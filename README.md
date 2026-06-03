# Vulkan Voxel Engine Dissertation

A performance-focused voxel rendering project built around a custom Vulkan engine. The project investigates how a chunk-based voxel world can be optimised for memory usage, chunk rebuild latency, and frame-time stability.

This repository is currently being cleaned up as a public portfolio case study.

## Project focus

The dissertation explored three main questions:

- How can GPU memory usage be reduced in a chunk-based voxel renderer?
- How quickly can dirty chunks be rebuilt after world edits?
- How stable are frame times under static, streaming, and edit-heavy workloads?

## Technical areas

- Vulkan rendering
- Chunk-based voxel terrain
- Greedy meshing
- Packed vertex formats
- Multithreaded chunk rebuild scheduling
- GPU buffer management
- Benchmark logging and CSV analysis
- Frame-time variance and percentile-based performance evaluation

## Current status

The project is complete as an academic dissertation project. The public repository is being prepared for portfolio use, including clearer documentation, setup notes, screenshots, benchmark summaries, and a concise case-study write-up.

## Planned cleanup

- Add build instructions
- Add screenshots or demo media
- Add benchmark summary
- Add architecture overview
- Add explanation of key systems
- Add known limitations and future work

## Portfolio note

This project is intended to demonstrate systems programming, graphics programming, profiling, optimisation, and the ability to evaluate engineering trade-offs with measurable evidence.
