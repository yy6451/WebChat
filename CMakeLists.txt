cmake_minimum_required(VERSION 3.10)
project(TinyWebChat VERSION 1.0)

# 根目录仅做聚合入口，单源构建在 backend/src
add_subdirectory(backend)