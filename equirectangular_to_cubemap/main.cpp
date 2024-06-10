#include "engine.h"

int main(int argc, char* argv[]) 
{

	MainEngine engine;

	engine.init();

	engine.run();

	engine.cleanup();

	return 0;
}