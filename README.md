[![Master branch Build Status](https://img.shields.io/github/actions/workflow/status/esp32m/core/ci.yml?branch=master&logo=github)](https://github.com/esp32m/core/actions?query=workflow%3A%22Continuous+Integration%22+branch%3Amaster)

# ESP32 Manager

## Visit our [Web site](//esp32m.com) for details

## Introduction

ESP32 Manager is a framework on top of [Espressif's](//espressif.com) native [ESP-IDF](//docs.espressif.com/projects/esp-idf) development environment,
designed to streamline firmware development for the most common use cases of home automation. It may be useful for those who want to explore ESP32 beyond the Arduino's simplicity and ESP-IDF's complexity.

Why another framework, if we alredy have [Arduino](//arduino.cc) and [ESP-IDF](//docs.espressif.com/projects/esp-idf)? My reasoning is explained in [this blog post.](/blog/initial) In short - I wanted something more comprehensive than Arduino, and more user-friendly than ESP-IDF. I wanted modular, re-usable code and UI to kickstart new projects easily, on a solid foundation, with all the bells and whistles like OTA, MQTT, WiFi autoconfig, remote logging, Web-based UI, JSON-based config & API, real-time monitoring, and more available to include (or exclude) at developer's discretion, with just a few lines of code. I wanted it to be fully event-driven, so the modules can asynchronously respond to events and talk to each other, instead of multiplying dependencies. I wanted true multitasking, to be able to monitor several sensors in parallel, not worrying about blocking other tasks, without dirty hacks. I wanted an API to supervise multiple ESP32-based devices remotely. 


## Architecture

ESP32 Manager consists of two major parts:

* **Core**
* **User Interface**

The core is written in C++, follows object-oriented pattern and includes the following building blocks:
* logging subsystem;
* event manager;
* configuration store;
* bus managers; 
* device drivers;
* network subsystem;
* input/output subsystem;
* debugging tools;
* user interface and API backed;
* bluetooth subsystem;
* application container.

The user interface is a [NodeJS](//nodejs.org/) library of [ReactJs](//reactjs.org/) components that interact with the **Core's** backend to visualize the state of the modules and pass user actions on to the module. The UI library follows the structure of the **Core**. Every stateful and/or inteactive module has corresponding visual component in the UI library. 
Compiled UI consists of two files - **index.html** and **main.js**, less than 250KB in size, compressed.

The size of ESP32 Manager in the compiled application (esp-idf runtime + esp32m core + UI) starts from 1.4MB.
