#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelFunkinOpponent);
	p->addModel(modelFunkinPlayer);
	p->addModel(modelFunkinDancer);
}
