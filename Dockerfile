# syntax=docker/dockerfile:1.7

FROM ubuntu:24.04 AS build

ARG VCPKG_BASELINE=cea592f4772491abdb7c483387a59ea89889f4be

ENV DEBIAN_FRONTEND=noninteractive
ENV VCPKG_ROOT=/opt/vcpkg
ENV VCPKG_MAX_CONCURRENCY=2

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        curl \
        git \
        ninja-build \
        pkg-config \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN git clone https://github.com/microsoft/vcpkg.git "${VCPKG_ROOT}" \
    && git -C "${VCPKG_ROOT}" checkout "${VCPKG_BASELINE}" \
    && "${VCPKG_ROOT}/bootstrap-vcpkg.sh"

WORKDIR /workspace

COPY CMakeLists.txt CMakePresets.json vcpkg.json vcpkg-configuration.json ./
COPY src ./src
COPY tests ./tests

RUN cmake --preset linux-vcpkg-headless
RUN cmake --build --preset linux-vcpkg-headless-build
RUN ctest --test-dir build/linux-vcpkg-headless --output-on-failure
