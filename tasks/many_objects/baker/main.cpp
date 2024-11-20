#include "Baker.hpp"
#include <spdlog/spdlog.h>

int main(int argc, char* argv[])
{
  if(argc != 2) 
  {
    spdlog::error("scene.gltf path is required");
    return 1;
  }

  std::filesystem::path path = std::filesystem::path(argv[1]);

  spdlog::info("run baker for " + path.filename().string());

  Baker baker{};
  baker.bakeScene(path);

  spdlog::info( "model baked" );

  return 0;
}