#include <iostream>
#include <sstream>

#include "Programmer.h"

int main()
{
    boost::shared_ptr<Programmer> pgm = Programmer::Open();
    if (!pgm)
    {
        std::cout << "Unable to find a programmer" << std::endl;
        return 1;
    }

    //enter terminal mode
    while(true)
    {
        std::cout << ">";

        std::string l;
        std::getline(std::cin, l);
        std::istringstream ls(l);

        std::string command;
        ls >> command;
        if (command == "exit")
        {
            break;
        }
        else if (command == "led")
        {
            std::string state;
            ls >> state;
            if (state == "on")
            {
                pgm->setLed(true);
            }
            else if (state == "off")
            {
                pgm->setLed(false);
            }
            else
            {
                std::cout << "Unknown LED state: \"" << state << "\"" << std::endl;
            }
        }
        else if (command == "read")
        {
            std::string reg;
            ls >> reg;
            if (reg == "t")
            {
                int i = pgm->readT();
                std::cout << "t: " << i << std::endl;
            }
            else
            {
                std::cout << "Unknown register: \"" << reg << "\"" << std::endl;
            }
        }
        else
        {
            std::cout << "Unknown command \"" << command << "\"" << std::endl;
        }
    }

    return 0;
}
