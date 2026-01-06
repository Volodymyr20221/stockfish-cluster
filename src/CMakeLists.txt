cmake_minimum_required(VERSION 3.21)
project(corrchess_client LANGUAGES CXX)

set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1/mingw_64")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Sql)

add_executable(sf_cluster_client
    main.cpp

    domain/domain_model.hpp
    domain/chess_san_to_fen.hpp
    domain/chess_san_to_fen.cpp

    infra/ServerConfigRepository.hpp
    infra/ServerConfigRepository.cpp
    infra/HistoryRepository.hpp
    infra/HistoryRepository.cpp

    app/ServerManager.hpp
    app/ServerManager.cpp
    app/JobManager.hpp
    app/JobManager.cpp

    net/JobConnection.hpp
    net/JobConnection.cpp
    net/JobNetworkController.hpp
    net/JobNetworkController.cpp

    ui/JobsModel.hpp
    ui/JobsModel.cpp
    ui/ServersModel.hpp
    ui/ServersModel.cpp
    ui/MainWindow.hpp
    ui/MainWindow.cpp

    ui/JobExporter.hpp
    ui/JobExporter.cpp

    ui/UiFormatters.hpp
    ui/UiFormatters.cpp

    ui/BoardWidget.hpp
    ui/BoardWidget.cpp
)

target_include_directories(sf_cluster_client PRIVATE .)

target_link_libraries(sf_cluster_client PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Sql
)
