# Vulkan Templates

This repository contains a collection of templates for Vulkan applications.

The templates are designed to be as simple as possible, while still providing a good starting point for a Vulkan application. The templates are written in C++.

# 1 Requirements

A GPU that supports Vulkan 1.3.
Visual Studio 2022.


# 2 Getting Started

This template is on the main branch and uses the following features/extensions:

	- Dynamic Rendering
	- Synchronization 2
	- Buffer Device Address
	- Descriptor Indexing
	- Descriptor Buffer (Replaces Descriptor Pools)
	- Shader Object (Replaces Traditional Pipeline)

# This Branch

This is a branch of the main repository that contains a simple Vulkan application that converts a 2D equirectangular image to a cubemap.

# TODO

	- Environment Map background based on currently loaded cubemap
	- Reflective Spheres (Reflections)
	- Metallic/Rough Spheres (Irradiance Mapping)