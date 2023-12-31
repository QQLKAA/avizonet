#include <avizonet.h>

#include <cerealizer.h>

#include <iostream>

namespace AvizoNet
{

void Hello()
{
    Cerealizer::Context ctx;
    ctx.Serialize<std::string>("Hello, world!");
    ctx.Rewind();

    std::string message;
    ctx.Deserialize(message);
    std::cout << message << std::endl;
}

}
