cmake_minimum_required(VERSION 3.16)
project(PoolBallRecognition LANGUAGES CXX)

find_package(OpenCV REQUIRED)
find_package(Threads REQUIRED)

# Match the Visual Studio project; "watershed main.cpp" is a separate example.
add_executable(PoolBallRecognition
    main.cpp
    MyApplication.cpp
    Binary.cpp
    CameraCalibration.cpp
    Edges.cpp
    Features.cpp
    Geometric.cpp
    Histograms.cpp
    Images.cpp
    Recognition.cpp
    Region.cpp
    Utilities.cpp
    Video.cpp
)
target_compile_features(PoolBallRecognition PRIVATE cxx_std_17)
set_target_properties(PoolBallRecognition PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(PoolBallRecognition PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(PoolBallRecognition PRIVATE ${OpenCV_LIBS} Threads::Threads)

# The program loads Media/ relative to its working directory.
add_custom_target(run
    COMMAND $<TARGET_FILE:PoolBallRecognition>
    DEPENDS PoolBallRecognition
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    USES_TERMINAL
    VERBATIM
)
