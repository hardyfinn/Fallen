#include "plugin.hpp"

Plugin* pluginInstance;

void init(Plugin* p) {
	pluginInstance = p;
	p->addModel(modelDURA);
	p->addModel(modelGROUP);
	p->addModel(modelPRIME);
	p->addModel(modelLANES);
	p->addModel(modelCYCLE);
	p->addModel(modelHOLD);
	p->addModel(modelFM4);
	p->addModel(modelCHORD);
	p->addModel(modelKIT);
	p->addModel(modelSPECTRA);
}
