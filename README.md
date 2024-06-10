# Vulkan Templates

This repository contains a collection of templates for Vulkan applications.

The templates are designed to be as simple as possible, while still providing a good starting point for a Vulkan application. The templates are written in C++.

# 1 Requirements

A GPU that supports Vulkan 1.3.
Visual Studio 2022 or later.


# 2 Getting Started

There is currently only 1 template. This template is on the main branch and uses the following features/extensions:

	- Dynamic Rendering
	- Synchronization 2
	- Buffer Device Address
	- Descriptor Indexing
	- Descriptor Buffer (Replaces Descriptor Pools)
	- Shader Object (Replaces Traditional Pipeline)

# This Branch

This branch is an example of how to use the template to create a simple Vulkan application. 
This application is used to convert a 2D equirectangular image to a cubemap, with ability to preview what the cubemap looks like.

# TODO

	- Environment Map the Cubemap
	- Reflective Spheres (Reflections)
	- Metallic/Rough Spheres (Irradiance)