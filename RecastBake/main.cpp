#include <cstdio>

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	std::printf("Usage: RecastBake <input.obj> <output.bin> --config <bake.toml>\n");
	return 2;
}
