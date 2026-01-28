#include <CarDemo.h>
#include <iostream>

int main()
{
    CarDemo app("Car Demo", 2000, 1000);
    if (!app.Initialize())
    {
        std::cout << "Failed to initialize application." << std::endl;
        return -1;
    }

    app.Run();

    return 0;
}
