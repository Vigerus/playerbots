#
# Copyright (C) 2005-2018 MaNGOS project <http://getmangos.com/>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

set(LIBRARY_NAME playerbots)

#Add PCH Files
set(bots_PCH "${CMAKE_CURRENT_SOURCE_DIR}/botpch.h")

#Base files
file(GLOB Playerbot_Source ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/*.h)
    source_group("Player Bot" FILES ${Playerbot_Source})

file(GLOB Playerbot_StateTree ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/*.h ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Tasks/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Tasks/*.h ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Providers/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Providers/*.h ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Conditions/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Conditions/*.h ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Considerations/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/playerbot/StateTree/Considerations/*.h)
    source_group("Player Bot\\StateTree" FILES ${Playerbot_StateTree})

file(GLOB Instancebot_Source ${CMAKE_CURRENT_SOURCE_DIR}/instancebot/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/instancebot/*.h)
    source_group("Instance Bot" FILES ${Instancebot_Source})

file(GLOB AHbot_Source ${CMAKE_CURRENT_SOURCE_DIR}/ahbot/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/ahbot/*.h)
    source_group("AH Bot" FILES ${AHbot_Source})

#Define base source library
set(LIBRARY_SRCS
    ${Playerbot_Source}
    ${Playerbot_StateTree}
    ${Instancebot_Source}
    ${AHbot_Source}
    ${bots_PCH}
    ${CMAKE_CURRENT_SOURCE_DIR}/botpch.cpp
)

# Define ENABLE_PLAYERBOTS if need
if (BUILD_PLAYERBOTS)
  add_definitions(-DENABLE_PLAYERBOTS)
endif()

# Define Core
if ( ${CMAKE_PROJECT_NAME} MATCHES "CMaNGOS")
  add_definitions(-DCMANGOS)
else()
  if ( ${CMAKE_PROJECT_NAME} MATCHES "MaNGOS")
    add_definitions(-DMANGOS)
  endif()
endif()

# Define Expansion
if ( ${CMAKE_PROJECT_NAME} MATCHES "Classic")
  add_definitions(-DMANGOSBOT_ZERO)
endif()
if ( ${CMAKE_PROJECT_NAME} MATCHES "TBC")
  add_definitions(-DMANGOSBOT_ONE)
endif()
if ( ${CMAKE_PROJECT_NAME} MATCHES "WoTLK")
  add_definitions(-DMANGOSBOT_TWO)
endif()

# Define ENABLE_MODULES if need
if (BUILD_MODULES)
  add_definitions(-DENABLE_MODULES)
endif()

# Define BUILD_AHBOT if need
if (BUILD_AHBOT)
  add_definitions(-DBUILD_AHBOT)
endif()

if(PCH)
  include_directories(${CMAKE_CURRENT_BINARY_DIR})
endif()

add_library(${LIBRARY_NAME} STATIC ${LIBRARY_SRCS})

target_link_libraries(${LIBRARY_NAME}
  PRIVATE shared
  PRIVATE Detour
  PRIVATE g3dlite
  PRIVATE zlib
)
if(WIN32)
  target_link_libraries(${LIBRARY_NAME}
    PRIVATE ws2_32
  )
endif()

target_include_directories(${LIBRARY_NAME}
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
)

# Install config files
if ( ${CMAKE_PROJECT_NAME} MATCHES "Classic")
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/playerbot/aiplayerbot.conf.dist.in ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist)
endif()	
if ( ${CMAKE_PROJECT_NAME} MATCHES "TBC")
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/playerbot/aiplayerbot.conf.dist.in.tbc ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist)
endif()	
if ( ${CMAKE_PROJECT_NAME} MATCHES "WoTLK")
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/playerbot/aiplayerbot.conf.dist.in.wotlk ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist)
endif()
if (NOT CONF_INSTALL_DIR)
  set(CONF_INSTALL_DIR ${CONF_DIR})
endif()
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/aiplayerbot.conf.dist DESTINATION ${CONF_INSTALL_DIR})
# STAI state tree brains (AiPlayerbot.StateTree.*), looked up next to aiplayerbot.conf
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/statetree DESTINATION ${CONF_INSTALL_DIR})

set_target_properties(${LIBRARY_NAME} PROPERTIES PROJECT_LABEL "PlayerBots")

# Generate PCH
if(PCH)
  target_precompile_headers(${LIBRARY_NAME} PRIVATE ${bots_PCH})
endif()

# Automation tests (Tests/*.spec.cpp, AutomationTest.h of the core): an object library linked into the
# AutomationTest executable, so the tests' static registrations are kept by the linker
file(GLOB Playerbot_Tests ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Tests/*.h)
if(Playerbot_Tests)
  source_group("Tests" FILES ${Playerbot_Tests})
  add_library(playerbots-tests OBJECT ${Playerbot_Tests})
  target_include_directories(playerbots-tests
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/src/automationtest
  )
  target_link_libraries(playerbots-tests
    PRIVATE shared
    PRIVATE Detour
    PRIVATE g3dlite
  )
  set_target_properties(playerbots-tests PROPERTIES PROJECT_LABEL "PlayerBotsTests")
endif()
