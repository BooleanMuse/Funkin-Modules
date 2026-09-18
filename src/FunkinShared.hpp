#pragma once
// ============================================================================
// What the two modules have in common: where characters come from, how one is
// loaded without stalling Rack, and the widget that puts it loose in the rack.
//
// The engine headers come first and Rack's come second, on purpose. plugin.hpp
// brings `using namespace rack` with it, and Rack has a namespace called
// `system` that collides with C's system(); anything that has to be readable
// without Rack is safer read before it.
// ============================================================================
#include "Chart.hpp"
#include "Character.hpp"
#include "Control.hpp"
#include "Layout.hpp"
#include "Library.hpp"
#include "Look.hpp"
#include "PanelDraw.hpp"
#include "SpriteDraw.hpp"

#include "plugin.hpp"

#include <osdialog.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace funkin {

// The panel is drawn by scaling millimetres; the knobs and jacks are placed with
// Rack's own mm2px(). If those two ever disagree the panel is drawn at one size
// and populated at another — which is exactly what happened once: the drawing
// came out three times too big, the module took sixty-odd HP of rack, and
// everything below about 40 mm fell off the bottom of a box that was still the
// right height. One line, checked at compile time, so it cannot go wrong quietly
// again.
static_assert(lay::kPxPerMm * lay::kHP > 14.99f && lay::kPxPerMm * lay::kHP < 15.01f,
              "lay::kPxPerMm disagrees with Rack: one HP must come out as 15 px, "
              "which is RACK_GRID_WIDTH");

// ---------------------------------------------------------------------------
// Threads
// ---------------------------------------------------------------------------

/** Runs something on a thread nobody keeps.

Two things about this are not optional, and both have cost a plugin its host
before. A std::thread that has finished is still joinable until somebody joins or
detaches it, and destroying a joinable thread calls std::terminate — which takes
Rack down with it, at closing time, every time. And an exception leaving a thread
does the same. So: detached the instant it exists, never stored, and the body
wrapped. */
inline void runDetached(std::function<void()> fn) {
	try {
		std::thread(
		    [fn]() {
			    try {
				    fn();
			    }
			    catch (...) {
				    // A mod folder that vanishes mid-scan is not worth the host.
			    }
		    })
		    .detach();
	}
	catch (...) {
		// The system would not give us a thread. The caller's job simply never
		// finishes, which its own "loading" state already covers.
	}
}

// ---------------------------------------------------------------------------
// The library, once, for every module in the patch
// ---------------------------------------------------------------------------

/** Where the plugin keeps characters of its own: drop a mod in here and both
modules find it with nothing to configure. */
inline std::string DefaultCharacterDir() {
	return asset::user("FunkinRack/characters");
}

/** ...and where it looks for a font to wear.

The plugin ships no font, and cannot: the Friday Night Funkin' lettering that is
going around is traced from the game and comes with either "free for personal
use" or no stated licence at all, and neither of those can be redistributed
inside a GPL plugin. Nothing stops *you* from using one, though. Drop a .ttf or
.otf in here and the panels put it on. */
inline std::string FontDir() {
	return asset::user("FunkinRack/fonts");
}

/** The first font file in the fonts folder, or empty.

Alphabetical, so which one you get is predictable when there are several, and it
is looked up once a second rather than every frame — this walks a directory, and
the panels draw sixty times a second. */
inline std::string UserFontPath(bool recheck = false) {
	static std::string cached;
	static double checkedAt = -1e9;
	double now = system::getTime();
	if (!recheck && now - checkedAt < 1.0)
		return cached;
	checkedAt = now;

	cached.clear();
	std::vector<std::string> entries = system::getEntries(FontDir());
	std::sort(entries.begin(), entries.end());
	for (const std::string& e : entries) {
		std::string ext = system::getExtension(e);
		for (char& c : ext)
			c = (char) std::tolower((unsigned char) c);
		if (ext == ".ttf" || ext == ".otf") {
			cached = e;
			break;
		}
	}
	return cached;
}

/** The two fonts the panels draw with.

`label` is always Rack's own Nunito, because everything the panel has to *tell*
you is set in it and it has to be readable at three millimetres. `display` is
your font if you have put one there.

The fallback is the part worth keeping: a traced game font has about seventy
glyphs in it, so a character called "daddy dearest" would come out half empty.
Handing NanoVG the label font as a fallback means anything the display font does
not have is quietly drawn in one that does. */
inline Fonts PanelFonts() {
	Fonts f;
	if (std::shared_ptr<window::Font> label =
	        APP->window->loadFont(asset::system("res/fonts/Nunito-Bold.ttf")))
		f.label = label->handle;
	f.display = f.label;

	std::string path = UserFontPath();
	if (!path.empty()) {
		if (std::shared_ptr<window::Font> display = APP->window->loadFont(path)) {
			f.display = display->handle;
			// Once per pair. The handles change when the graphics context is
			// rebuilt, and adding the same fallback twice would stack them up.
			static int lastDisplay = -2, lastLabel = -2;
			if ((f.display != lastDisplay || f.label != lastLabel) && f.label >= 0 &&
			    f.display != f.label) {
				nvgAddFallbackFontId(APP->window->vg, f.display, f.label);
				lastDisplay = f.display;
				lastLabel = f.label;
			}
		}
	}
	return f;
}

struct ScanJob {
	std::vector<std::string> folders;
	fnf::Library lib;
	std::atomic<bool> done{false};
};

/** One library for the whole patch.

Shared, rather than one per module, for a reason the user can feel: you add your
mod folder once and every Funkin' module in the rack has it. The folder list is
saved by each module, and loading a patch merges them, so a patch carries its own
mods with it.

Graphics thread only. The audio thread never looks at a character. */
struct Hub {
	fnf::Library lib;
	std::vector<std::string> folders;
	std::shared_ptr<ScanJob> job;
	bool started = false;

	void ensureStarted() {
		if (started)
			return;
		started = true;
		std::string home = DefaultCharacterDir();
		system::createDirectories(home);
		addFolder(home);
	}

	bool hasFolder(const std::string& f) const {
		for (const std::string& s : folders) {
			if (s == f)
				return true;
		}
		return false;
	}

	void addFolder(const std::string& f) {
		if (f.empty() || hasFolder(f))
			return;
		folders.push_back(f);
		rescan();
	}

	void removeFolder(const std::string& f) {
		for (size_t i = 0; i < folders.size(); i++) {
			if (folders[i] == f) {
				folders.erase(folders.begin() + (long) i);
				rescan();
				return;
			}
		}
	}

	/** Walks the folders on a thread of its own. A real mod is thousands of
	files, and doing that between two frames is a visible stall — worse, it would
	happen again every time somebody opened the character menu. */
	void rescan() {
		if (job)
			return;  // one at a time; the next request rides on this one
		auto j = std::make_shared<ScanJob>();
		j->folders = folders;
		job = j;
		runDetached([j]() {
			j->lib.scan(j->folders);
			j->done.store(true, std::memory_order_release);
		});
	}

	/** Called from a widget's step(). Picks up a finished scan, and starts
	another if the folders changed while that one was running.

	That second half matters more than it looks: loading a patch adds every
	folder the patch knew about, one module at a time, while the first scan is
	still walking. Without this, all but the first request would be dropped and
	half the characters would be missing until somebody asked again by hand. */
	void poll() {
		if (!job || !job->done.load(std::memory_order_acquire))
			return;
		lib = std::move(job->lib);
		bool stale = job->folders != folders;
		job.reset();
		if (stale)
			rescan();
	}

	bool scanning() const { return job != nullptr; }
};

inline Hub& hub() {
	static Hub h;
	return h;
}

// ---------------------------------------------------------------------------
// Wearing a character
// ---------------------------------------------------------------------------

struct LoadJob {
	fnf::CharacterRef ref;
	bool builtin = false;
	fnf::Character character;
	fnf::Image sheet;
	fnf::Image icon;
	std::string error;
	bool ok = false;
	std::atomic<bool> done{false};
};

/** Everything a module needs in order to be somebody: the character, its pixels,
and what it is doing right now.

Lives in the Module and is touched only from the graphics thread. The audio
thread posts what it wants sung through a ring and never looks in here — a
spritesheet is tens of megabytes and loading one is a disk read, which is the
last thing that should ever happen between two samples. */
struct CharacterSlot {
	fnf::Character character;
	fnf::Image sheet;
	fnf::Image icon;
	fnf::CharacterState state;

	/** The icon, on the graphics card. It lives on the slot rather than on the
	panel widget for one reason: Rack has a single NanoVG context for the whole
	window, so a handle made by one widget is readable by any other — and that is
	how the player module puts its neighbour's face on the health bar. */
	int iconImage = -1;
	int iconW = 0, iconH = 0, iconFrames = 1;
	uint64_t iconUploaded = 0;

	/** What the patch saves. Empty means the built-in one. */
	std::string key;
	fnf::CharacterRef ref;
	std::string error;
	std::shared_ptr<LoadJob> job;
	/** Bumped whenever the character changes, so widgets know their uploaded
	textures belong to somebody else now. */
	uint64_t generation = 1;

	bool loading() const { return job != nullptr; }
	bool ready() const { return character.loaded(); }

	void request(const fnf::CharacterRef& r) {
		auto j = std::make_shared<LoadJob>();
		j->ref = r;
		job = j;
		key = r.key();
		ref = r;  // kept: it is how the sheet is read again if the context goes
		runDetached([j]() {
			// Everything slow in one place: the XML, the character file, and a
			// PNG that can be four thousand pixels square.
			j->ok = fnf::LoadCharacter(j->ref, &j->character, &j->error);
			if (j->ok) {
				std::string err;
				if (!j->sheet.load(j->character.imagePath, &err)) {
					j->ok = false;
					j->error = err;
				}
				if (!j->character.iconPath.empty())
					j->icon.load(j->character.iconPath, &err);  // an icon is optional
			}
			j->done.store(true, std::memory_order_release);
		});
	}

	/** Asks for whatever `key` says, once the library has been scanned. */
	void requestByKey(const std::string& want) {
		if (want.empty()) {
			// Nobody. The panel says so and the rack stays empty, which is the
			// honest picture when there are no mods to pick from.
			character = fnf::Character();
			sheet = fnf::Image();
			icon = fnf::Image();
			key.clear();
			ref = fnf::CharacterRef();
			generation++;
			return;
		}
		const fnf::CharacterRef* r = hub().lib.find(want);
		if (!r) {
			// The patch names a character this machine does not have. Whoever we
			// were stays on, and the panel says which mod is missing: the patch
			// is not broken, the mod is simply not installed here.
			key = want;
			error = "not installed: " + want;
			return;
		}
		request(*r);
	}

	/** Graphics thread. Takes delivery of a finished load. */
	bool poll() {
		if (!job || !job->done.load(std::memory_order_acquire))
			return false;
		std::shared_ptr<LoadJob> j = job;
		job.reset();
		if (!j->ok) {
			error = j->error.empty() ? std::string("could not open that character") : j->error;
			// Whatever we were wearing before stays on: swapping to nobody
			// because the new one failed would lose two things at once.
			return true;
		}
		character = std::move(j->character);
		sheet = std::move(j->sheet);
		icon = std::move(j->icon);
		error.clear();
		state = fnf::CharacterState();
		generation++;
		return true;
	}

	/** The sheet again, after the graphics card lost it. The pixels are let go
	the moment they are uploaded — a sheet is the single biggest thing in this
	plugin — so this is how they come back. */
	void reloadPixels() {
		if (job || ref.imagePath.empty())
			return;
		request(ref);
	}

	/** Puts the icon on the graphics card if it is not there already. Safe to
	call every frame from every widget that wants it. */
	void ensureIcon(NVGcontext* vg) {
		if (iconUploaded == generation)
			return;
		if (iconImage >= 0) {
			nvgDeleteImage(vg, iconImage);
			iconImage = -1;
		}
		if (icon.hasPixels()) {
			iconImage = nvgCreateImageRGBA(vg, icon.w, icon.h, 0, icon.rgba.data());
			iconW = icon.w;
			iconH = icon.h;
			iconFrames = fnf::IconLayout(icon).count;
		}
		iconUploaded = generation;
	}

	/** The graphics context went away and took the handle with it. The pixels
	are still here — an icon is small enough to keep — so it only has to be
	uploaded again. */
	void forgetIcon() {
		iconImage = -1;
		iconUploaded = 0;
	}

	/** Fills in the plate: name, mod, icon and colour, from wherever this
	character got to. Shared so that a module can draw its neighbour's card with
	the same code it draws its own. */
	void fillCard(CharacterCard* card, const std::string& nameBuf,
	              const std::string& modBuf) const {
		card->name = nameBuf.c_str();
		card->mod = modBuf.c_str();
		card->loading = job != nullptr && !character.loaded();
		card->iconImage = iconImage;
		card->iconW = iconW;
		card->iconH = iconH;
		card->iconFrames = iconFrames;
		card->color = nvgRGB(character.barColor[0], character.barColor[1], character.barColor[2]);
	}

	float singSeconds(float bpm, float multiplier) const {
		// The character file gives the hold in *steps*, so it follows the tempo:
		// a character tuned to hold for 6.1 steps holds for 6.1 steps whatever
		// the module is set to.
		float steps = character.loaded() ? character.singDuration : 4.f;
		return steps * fnf::SecondsPerStep(bpm) * multiplier;
	}
};

// ---------------------------------------------------------------------------
// Talking to the audio thread
// ---------------------------------------------------------------------------

/** What the audio thread tells the graphics thread to do with the character.

The game runs in process(), because a hit window is measured in milliseconds and
a frame is not. The animation runs in the graphics thread, because that is where
frames are. This ring is the whole of the connection between them: single writer,
single reader, no locks, and if it ever overflows the newest survive — a stall of
half a second should drop the singing that happened during it, not queue it up to
be mimed afterwards. */
struct SingRing {
	enum Kind : int8_t {
		kSing = 0,
		kMiss,
		kBeat,
		kHey,
	};
	struct Event {
		int8_t kind = kSing;
		int8_t dir = 0;
		float hold = 0.f;
	};
	static const int kSize = 64;

	Event slots[kSize];
	std::atomic<uint32_t> writeIndex{0};
	uint32_t readIndex = 0;

	void push(int8_t kind, int8_t dir, float hold) {
		uint32_t w = writeIndex.load(std::memory_order_relaxed);
		slots[w % kSize] = Event{kind, dir, hold};
		writeIndex.store(w + 1, std::memory_order_release);
	}

	bool pop(Event* out) {
		uint32_t w = writeIndex.load(std::memory_order_acquire);
		if (readIndex == w)
			return false;
		if (w - readIndex > (uint32_t) kSize) {
			// Overrun: the writer has been round more than once. Skip to what
			// still exists rather than reading over the writer's shoulder.
			readIndex = w - kSize;
		}
		*out = slots[readIndex % kSize];
		readIndex++;
		return true;
	}
};

/** The two lines of the character plate.

Wearing nobody is a real state now that there is no built-in stand-in, and it has
to read as "here is what to do" rather than as a module that failed to draw. So
the plate says so, and says which of the three reasons it is. */
inline void CharacterCardText(const CharacterSlot& slot, std::string* name, std::string* mod) {
	if (slot.ready()) {
		*name = slot.character.name;
		*mod = slot.error.empty() ? slot.character.modName : slot.error;
		return;
	}
	*name = "no character";
	if (!slot.error.empty())
		*mod = slot.error;
	else if (hub().scanning())
		*mod = "looking for mods...";
	else if (hub().lib.entries.empty())
		*mod = "right-click: add a mod folder";
	else
		*mod = "right-click to pick one";
}

/** Who to wear when nobody has said. The choosing is in the engine, where it can
be tested without Rack; this only hands it the shelves. */
inline std::string PickDefaultCharacterKey(int role) {
	return fnf::PickDefaultKey(hub().lib.entries, role);
}

/** Both modules are one of these, so that either can ask the other who it is
wearing. A cable carries gates and not a face, so a module standing next to
another is the only way the health bar can learn both ends of the duel. */
struct CharacterHost {
	virtual ~CharacterHost() {}
	virtual CharacterSlot* characterSlot() = 0;
};

/** Left and right change places on a mirrored character.

This is the game's own rule and not a nicety: a character drawn facing one way
and used on the other side is flipped, and its singLEFT is then pointing right.
Flipping the picture without swapping the animations gives a character who
reaches the wrong way for every note. */
inline int MirrorDir(int dir, bool mirror) {
	if (!mirror)
		return dir;
	if (dir == fnf::kLeft)
		return fnf::kRight;
	if (dir == fnf::kRight)
		return fnf::kLeft;
	return dir;
}

/** Key presses on their way from the graphics thread to the audio thread.

The keyboard arrives where Rack delivers it, which is the graphics thread, and
the arrows are judged where the notes are, which is the audio thread. One ring,
single writer, single reader, same as the singing goes the other way. */
struct KeyRing {
	static const int kSize = 32;
	fnf::ArrowEvent slots[kSize];
	std::atomic<uint32_t> writeIndex{0};
	uint32_t readIndex = 0;

	void push(int number, float value) {
		fnf::ArrowEvent e;
		e.type = fnf::kBindKey;
		e.channel = -1;
		e.number = number;
		e.value = value;
		uint32_t w = writeIndex.load(std::memory_order_relaxed);
		slots[w % kSize] = e;
		writeIndex.store(w + 1, std::memory_order_release);
	}

	bool pop(fnf::ArrowEvent* out) {
		uint32_t w = writeIndex.load(std::memory_order_acquire);
		if (readIndex == w)
			return false;
		if (w - readIndex > (uint32_t) kSize)
			readIndex = w - kSize;
		*out = slots[readIndex % kSize];
		readIndex++;
		return true;
	}
};

/** The beat, from a clock input if there is one and from a knob if there is not.

Both modules have this, and it is the thing that makes a character *bop* rather
than stand still: in the game an idle is not a loop, it is one animation played
once per beat. */
struct BeatClock {
	float phase = 0.f;  // 0..1 through the current beat
	dsp::SchmittTrigger trigger;
	bool external = false;
	/** Beats since the module was made. Read by the panel for the pulsing dot. */
	std::atomic<uint32_t> count{0};

	/** Returns true on the beat. */
	bool process(float dt, float bpm, bool clockConnected, float clockVoltage) {
		external = clockConnected;
		if (external) {
			// An external clock owns the beat outright: a phase that also
			// advanced on its own would drift against it and double-trigger.
			if (trigger.process(clockVoltage, 0.1f, 2.f)) {
				phase = 0.f;
				count.fetch_add(1, std::memory_order_relaxed);
				return true;
			}
			// Kept running for the panel's dot, so the pulse decays between
			// clock pulses instead of sitting at full brightness.
			phase += dt * bpm / 60.f;
			if (phase > 1.f)
				phase = 1.f;
			return false;
		}
		phase += dt * bpm / 60.f;
		if (phase >= 1.f) {
			phase -= 1.f;
			if (phase >= 1.f)
				phase = 0.f;  // a wild tempo change should not run the beat away
			count.fetch_add(1, std::memory_order_relaxed);
			return true;
		}
		return false;
	}
};

// ---------------------------------------------------------------------------
// The character, loose in the rack
// ---------------------------------------------------------------------------

/** Draws the character over the rack, outside its own module.

Hung on APP->scene->rack rather than on the panel, which is the whole point: the
character is in the rack, at whatever size you like, and moving it does not move
anything else. It takes a click only when the click is on the character — a
widget this size that swallowed clicks would make everything underneath it
unusable, and the modules underneath are the patch. */
struct CharacterOverlay : widget::Widget {
	Module* module = nullptr;
	CharacterSlot* slot = nullptr;
	/** Where it stands, in rack coordinates, and whether the user has ever said
	so. Owned by the module so that the patch remembers. */
	float* posX = nullptr;
	float* posY = nullptr;
	bool* pinned = nullptr;
	float* height = nullptr;  // in rack pixels
	bool* mirror = nullptr;
	bool* showName = nullptr;
	/** The knob the size lives on, so that rolling the wheel over the character
	moves the same value the panel does. There is no second copy of the size and
	nothing to keep in step. */
	int sizeParamId = -1;
	/** Which way the character stands off its own module: -1 for the opponent,
	which goes to its left, and +1 for the player, which goes to its right. Put
	the two modules side by side and the characters end up at the two outsides,
	facing each other, the way the game has them. */
	float sideSign = -1.f;

	int image = -1;
	int imageW = 0, imageH = 0;
	uint64_t uploaded = 0;
	bool dragging = false;
	double lastTime = 0.0;

	void onContextDestroy(const ContextDestroyEvent& e) override {
		// The texture belonged to a context that is gone. The pixels went with
		// it the moment they were uploaded, so the sheet has to be read again.
		image = -1;
		uploaded = 0;
		if (slot)
			slot->reloadPixels();
		Widget::onContextDestroy(e);
	}

	/** Where the character stands if nobody has moved it: beside its own module,
	feet on the rail its module sits on.

	Beside, and not in front of. Standing them on their own module's bottom edge
	puts the whole body over the panel — which hides the thing you have to read
	in order to play. Following the module until they are picked up is also what
	keeps two of these from landing on top of one another. */
	void followModule() {
		if (!module || !pinned || *pinned)
			return;
		ModuleWidget* mw = APP->scene->rack->getModule(module->id);
		if (!mw)
			return;
		float w = characterWidth();
		if (sideSign == 0.f) {
			// Straight up: standing on the module's own roof, centred. That is
			// where the one in the background belongs — behind the fight and
			// higher than it — and it is the only way a character three times
			// wider than a 12 HP panel can be near its module without covering
			// the panel completely.
			*posX = mw->box.getCenter().x;
			*posY = mw->box.pos.y;
			return;
		}
		*posX = (sideSign < 0.f) ? mw->box.pos.x - w * 0.55f
		                         : mw->box.pos.x + mw->box.size.x + w * 0.55f;
		*posY = mw->box.pos.y + mw->box.size.y;
	}

	/** How wide the character is at the height it is being drawn at. Needed
	before the position is known, so it cannot come from characterRect(). */
	float characterWidth() const {
		if (!slot || !slot->ready() || !height)
			return 0.f;
		float nh = slot->character.nominalHeight();
		float nw = slot->character.nominalWidth();
		if (nh <= 0.f)
			return 0.f;
		return *height * nw / nh;
	}

	Rect characterRect() const {
		if (!slot || !slot->ready() || !posX)
			return Rect();
		float w = characterWidth();
		if (w <= 0.f)
			return Rect();
		return Rect(Vec(*posX - w * 0.5f, *posY - *height), Vec(w, *height));
	}

	void step() override {
		Widget::step();
		if (!slot)
			return;
		// The rack is our box. Rack only offers a click to a child whose box
		// contains it, so an overlay with no size would never hear one — and the
		// character could not be picked up or right-clicked.
		box.pos = Vec(0, 0);
		box.size = parent ? parent->box.size : Vec(1e5f, 1e5f);
		followModule();

		// The animation runs on wall time rather than on a frame count: two
		// machines at different frame rates should see the same sing last the
		// same length of time.
		double now = system::getTime();
		float dt = (lastTime > 0.0) ? (float) (now - lastTime) : 0.f;
		lastTime = now;
		if (dt > 0.25f)
			dt = 0.25f;  // came back from a stall; do not fast-forward the pose
		if (slot->ready())
			slot->state.advance(dt, slot->character);
	}

	void draw(const DrawArgs& args) override;
	void onButton(const event::Button& e) override;
	void onHoverScroll(const event::HoverScroll& e) override;
	void onDragStart(const event::DragStart& e) override;
	void onDragMove(const event::DragMove& e) override;
	void onDragEnd(const event::DragEnd& e) override;
	/** Filled in by whichever module owns this, so right-clicking the character
	offers the same things right-clicking the panel does. */
	std::function<void(ui::Menu*)> menuBuilder;
};

inline void CharacterOverlay::draw(const DrawArgs& args) {
	if (!slot || !slot->ready())
		return;
	NVGcontext* vg = args.vg;

	if (uploaded != slot->generation || image < 0) {
		if (image >= 0) {
			nvgDeleteImage(vg, image);
			image = -1;
		}
		if (slot->sheet.hasPixels()) {
			// NEAREST for a character whose mod says so: a pixel-art character
			// scaled up smoothly is a blurry mess, and "no_antialiasing" in the
			// character file is the author saying exactly that.
			int flags = slot->character.antialias ? 0 : NVG_IMAGE_NEAREST;
			image = nvgCreateImageRGBA(vg, slot->sheet.w, slot->sheet.h, flags,
			                           slot->sheet.rgba.data());
			imageW = slot->sheet.w;
			imageH = slot->sheet.h;
			uploaded = slot->generation;
			// The graphics card has it now. A spritesheet is the biggest thing
			// in this plugin and there is no reason for two copies of it.
			slot->sheet.release();
		}
	}
	if (image < 0)
		return;

	int animIndex = slot->state.currentAnim(slot->character);
	if (animIndex < 0)
		return;
	Rect r = characterRect();
	if (r.size.x <= 0.f)
		return;

	bool flip = slot->character.flipX;
	if (mirror && *mirror)
		flip = !flip;

	DrawCharacterFrame(vg, image, imageW, imageH, slot->character, animIndex,
	                   slot->state.currentFrame(slot->character), *posX, *posY, r.size.y, flip);

	if (showName && *showName) {
		// The character's own name over its head: the display font, same as the
		// name on the plate.
		int font = PanelFonts().display;
		punchText(vg, font, *posX, r.pos.y - 4.f, 13.f,
		          NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM, nvgRGB(0xff, 0xff, 0xff),
		          slot->character.name.c_str());
	}
}

inline void CharacterOverlay::onButton(const event::Button& e) {
	if (!slot || !slot->ready())
		return;
	Rect r = characterRect();
	if (r.size.x <= 0.f || !r.contains(e.pos))
		return;  // not on the character: the click belongs to whatever is under it

	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
		e.consume(this);
		return;
	}
	if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
		e.consume(this);
		if (menuBuilder) {
			ui::Menu* menu = createMenu();
			menuBuilder(menu);
		}
		return;
	}
}

/** The wheel, over the character, resizes it.

The SIZE knob on the panel does the same thing and always did, but a character
standing three modules away from its own panel is not somewhere you look for a
knob — you reach for the thing itself. This drives the very same parameter, so it
is saved in the patch, shows on the knob, and can be automated like any other.

The scroll is only taken when the pointer is actually on the character; anywhere
else it belongs to Rack, which uses it to zoom. */
inline void CharacterOverlay::onHoverScroll(const event::HoverScroll& e) {
	if (!slot || !slot->ready() || !module || sizeParamId < 0)
		return;
	Rect r = characterRect();
	if (r.size.x <= 0.f || !r.contains(e.pos))
		return;
	if (sizeParamId >= (int) module->paramQuantities.size())
		return;
	ParamQuantity* pq = module->paramQuantities[(size_t) sizeParamId];
	if (!pq)
		return;
	// A twentieth of the range per notch: fine enough to land on a size you
	// meant, coarse enough to cross the whole range in a flick.
	float step = (pq->getMaxValue() - pq->getMinValue()) / 20.f;
	float v = pq->getValue() + (e.scrollDelta.y > 0.f ? step : -step);
	pq->setValue(math::clamp(v, pq->getMinValue(), pq->getMaxValue()));
	e.consume(this);
}

inline void CharacterOverlay::onDragStart(const event::DragStart& e) {
	if (e.button == GLFW_MOUSE_BUTTON_LEFT)
		dragging = true;
}

inline void CharacterOverlay::onDragMove(const event::DragMove& e) {
	if (!dragging || !posX)
		return;
	// The delta has to be divided by the zoom: it arrives in screen pixels and
	// these coordinates are the rack's.
	float zoom = getAbsoluteZoom();
	if (zoom <= 0.f)
		zoom = 1.f;
	*posX += e.mouseDelta.x / zoom;
	*posY += e.mouseDelta.y / zoom;
	if (pinned)
		*pinned = true;  // moved by hand: stop following the module
}

inline void CharacterOverlay::onDragEnd(const event::DragEnd& e) {
	dragging = false;
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

/** The character picker, the mod folders, and everything else both modules put
in their right-click menu. `onPick` is handed the key to wear. */
inline void AppendCharacterMenu(ui::Menu* menu, CharacterSlot* slot,
                                std::function<void(const std::string&)> onPick) {
	Hub& h = hub();

	menu->addChild(createMenuLabel("Character"));
	if (h.scanning()) {
		menu->addChild(createMenuLabel("looking for mods..."));
	}
	else if (h.lib.entries.empty()) {
		menu->addChild(createMenuLabel("no characters found yet"));
	}
	else {
		// One submenu per mod. A folder of mods can hold hundreds of characters
		// and a flat list of those is not a menu, it is a wall.
		std::vector<std::string> mods;
		for (const fnf::CharacterRef& r : h.lib.entries) {
			if (mods.empty() || mods.back() != r.modName)
				mods.push_back(r.modName);
		}
		for (const std::string& mod : mods) {
			menu->addChild(createSubmenuItem(mod, "", [mod, slot, onPick](ui::Menu* sub) {
				for (const fnf::CharacterRef& r : hub().lib.entries) {
					if (r.modName != mod)
						continue;
					std::string key = r.key();
					std::string name = r.name;
					if (r.jsonPath.empty())
						name += "  (sheet only)";
					sub->addChild(createCheckMenuItem(
					    name, "", [slot, key]() { return slot->key == key; },
					    [onPick, key]() { onPick(key); }));
				}
			}));
		}
	}

	menu->addChild(new ui::MenuSeparator);
	menu->addChild(createMenuLabel("Mods"));
	menu->addChild(createMenuItem("Open the characters folder", "", []() {
		std::string dir = DefaultCharacterDir();
		system::createDirectories(dir);
		system::openDirectory(dir);
	}));
	menu->addChild(createMenuItem("Add a mod folder...", "", []() {
		char* path = osdialog_file(OSDIALOG_OPEN_DIR, DefaultCharacterDir().c_str(), NULL, NULL);
		if (!path)
			return;
		std::string chosen = path;
		std::free(path);
		hub().addFolder(chosen);
	}));
	menu->addChild(createMenuItem("Look again", "", []() { hub().rescan(); }));

	menu->addChild(new ui::MenuSeparator);
	{
		// The font is the same shape of problem as a mod: something you own, that
		// the plugin can use but must not carry.
		std::string path = UserFontPath();
		std::string name = path.empty() ? std::string("Rack's own")
		                                : system::getFilename(path);
		menu->addChild(createMenuLabel("Font: " + name));
		menu->addChild(createMenuItem("Open the fonts folder", "", []() {
			system::createDirectories(FontDir());
			system::openDirectory(FontDir());
		}));
		menu->addChild(createMenuItem("Look for a font again", "", []() {
			UserFontPath(true);
		}));
	}

	if (hub().folders.size() > 1) {
		menu->addChild(createSubmenuItem("Folders being watched", "", [](ui::Menu* sub) {
			std::string home = DefaultCharacterDir();
			for (const std::string& f : hub().folders) {
				if (f == home) {
					sub->addChild(createMenuLabel(f + "   (the plugin's own)"));
					continue;
				}
				sub->addChild(createMenuItem(f, "remove", [f]() { hub().removeFolder(f); }));
			}
		}));
	}
	if (!hub().lib.note.empty())
		menu->addChild(createMenuLabel(hub().lib.note));
	if (!slot->error.empty())
		menu->addChild(createMenuLabel(slot->error));
}

/** Everything a module has to do on the graphics thread, once a frame: take
delivery of a finished scan or a finished character, pick one if it has never
been told, and turn what the audio thread sang into poses.

All three modules call this and nothing else, which is the point — three copies
of it would be three chances for one of them to stop bopping. */
inline void StepCharacterHome(CharacterSlot& slot, SingRing& ring, bool mirror,
                              std::string& wantedKey, bool& wantedPending, bool& autoPicked,
                              int role) {
	hub().ensureStarted();
	hub().poll();
	slot.poll();

	if (wantedPending && !hub().scanning()) {
		wantedPending = false;
		if (slot.key != wantedKey)
			slot.requestByKey(wantedKey);
	}
	else if (!autoPicked && !wantedPending && !hub().scanning() && !slot.loading() &&
	         slot.key.empty()) {
		// Never been told, and the shelves have been counted. Wear somebody
		// real if there is anybody real to wear.
		autoPicked = true;
		std::string key = PickDefaultCharacterKey(role);
		if (!key.empty())
			slot.requestByKey(key);
	}

	SingRing::Event e;
	while (ring.pop(&e)) {
		if (!slot.ready())
			continue;
		int dir = MirrorDir(e.dir, mirror);
		switch (e.kind) {
			case SingRing::kSing: slot.state.sing(dir, e.hold); break;
			case SingRing::kMiss: slot.state.miss(dir, e.hold); break;
			case SingRing::kBeat: slot.state.beat(slot.character); break;
			case SingRing::kHey: {
				int hey = slot.character.slot[fnf::kAnimHey];
				if (hey >= 0)
					slot.state.play(hey);
				break;
			}
			default: break;
		}
	}
}

/** The animations this character can play, so that a "hey", a "scared" or
whatever else a mod put in the sheet is reachable rather than dead weight. */
inline void AppendAnimationMenu(ui::Menu* menu, CharacterSlot* slot) {
	if (!slot->ready())
		return;
	std::vector<int> playable = slot->character.playableAnims();
	if (playable.empty())
		return;
	menu->addChild(createSubmenuItem("Play an animation", "", [slot, playable](ui::Menu* sub) {
		for (int i : playable) {
			std::string name = slot->character.anims[(size_t) i].name;
			sub->addChild(createMenuItem(name, "", [slot, i]() { slot->state.play(i); }));
		}
	}));
}

}  // namespace funkin
