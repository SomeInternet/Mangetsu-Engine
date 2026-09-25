Mangetsu Engine
=====================
T Fong `SomeInternetBoi`  
[My Website](https://tzfong.com/), [My LinkedIn](https://www.linkedin.com/in/tzfong/)

A light Vulkan pathtracer engine using Vulkan's hardware raytracing API, written in C++ and Slang.

### (Planned) Features

#### Hardware Ray Tracing

#### Physically-based Materials (Cook-Torrance BSDF, GGX Microfacet Distribution)

#### glTF Loading

#### Physically-based Camera (Thick Lens Approximation)

#### Next Event Estimation (Light Tree)

### Third-Party Libraries
**VkBootstrap**  
Helper classes and functions to reduce the boilerplate of picking a Vulkan device and setting up Vulkan objects.

**SDL**  
For windowing, setting up a surface for Vulkan to present to.

**ImGUI**  
For the, erm, GUI.

**TinyglTF**  
For glTF loading.

**VMA**  
For managing memory allocations.

**STB**  
For image loading.

### Attributions
**VkGuide**  
Provided resources on engine architecture and setup that I took inspiration from.

**Vulkan Docs Tutorial**  
Provided resources and sample code for writing a modern Vulkan application, engine architecture, glTF loading, and raytracing that were extremely helpful.

**University of Pennsylvania, CIS 4610/5610 Curriculum**  
Provided some resources on pathtracing I used.

**Claude**  
I used Claude to set up the CMakeLists and dependencies, bounced a lot of my ideas off Claude in this project, and used it to help me set up
the Vulkan Raytracing API boilerplate and debug my code.