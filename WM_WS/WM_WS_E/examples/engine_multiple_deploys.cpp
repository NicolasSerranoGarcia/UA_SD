#include "engine.hpp"

#include <iostream>
#include <chrono>
#include <thread>
#include <syncstream>

using namespace std::literals;

int main(){
    Engine engine;
    Engine engine1;
    
    auto e = engine.start();
    std::this_thread::sleep_for(2s);
    auto r = engine1.start();

    if(!r && r.error().type == Engine::ErrorType::Fault){
        std::cerr << "Error instantiating engine1" << std::endl;
        return 1;
    }
    std::jthread t1([&](){
        std::osyncstream out{std::cout};
        while(true){
            std::this_thread::sleep_for(10ms);
            out << "engine: " << engine.remainingTimeToShutdown() << "\nengine1: " << engine1.remainingTimeToShutdown() << std::endl;
            out.emit();
        }
    });

    std::jthread t2([&](){
        std::osyncstream out{std::cout};
        while(true){
            std::this_thread::sleep_for(10ms);
            out << "engine flow: " << engine.measureFlow().currentFlux << "\nengine1 flow: " << engine1.measureFlow().currentFlux << std::endl;
            out.emit();
        }
    });


}