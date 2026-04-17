# Locate SDL3 library
# This module defines
# SDL3_LIBRARY, the name of the library to link against
# SDL3_FOUND, if false, do not try to link to SDL3
# SDL3_INCLUDE_DIR, where to find SDL.h
#
# This module responds to the the flag:
# SDL3_BUILDING_LIBRARY
# If this is defined, then no SDL3main will be linked in because
# only applications need main().
# Otherwise, it is assumed you are building an application and this
# module will attempt to locate and set the the proper link flags
# as part of the returned SDL3_LIBRARY variable.
#
# Don't forget to include SDLmain.h and SDLmain.m your project for the
# OS X framework based version. (Other versions link to -lSDL3main which
# this module will try to find on your behalf.) Also for OS X, this
# module will automatically add the -framework Cocoa on your behalf.
#
#
# Additional Note: If you see an empty SDL3_LIBRARY_TEMP in your configuration
# and no SDL3_LIBRARY, it means CMake did not find your SDL3 library
# (SDL3.dll, libsdl3.so, SDL3.framework, etc).
# Set SDL3_LIBRARY_TEMP to point to your SDL3 library, and configure again.
# Similarly, if you see an empty SDL3MAIN_LIBRARY, you should set this value
# as appropriate. These values are used to generate the final SDL3_LIBRARY
# variable, but when these values are unset, SDL3_LIBRARY does not get created.
#
#
# $SDL3DIR is an environment variable that would
# correspond to the ./configure --prefix=$SDL3DIR
# used in building SDL3.
# l.e.galup  9-20-02
#
# Modified by Eric Wing.
# Added code to assist with automated building by using environmental variables
# and providing a more controlled/consistent search behavior.
# Added new modifications to recognize OS X frameworks and
# additional Unix paths (FreeBSD, etc).
# Also corrected the header search path to follow "proper" SDL guidelines.
# Added a search for SDL3main which is needed by some platforms.
# Added a search for threads which is needed by some platforms.
# Added needed compile switches for MinGW.
#
# On OSX, this will prefer the Framework version (if found) over others.
# People will have to manually change the cache values of
# SDL3_LIBRARY to override this selection or set the CMake environment
# CMAKE_INCLUDE_PATH to modify the search paths.
#
# Note that the header path has changed from SDL3/SDL.h to just SDL.h
# This needed to change because "proper" SDL convention
# is #include "SDL.h", not <SDL3/SDL.h>. This is done for portability
# reasons because not all systems place things in SDL3/ (see FreeBSD).

#=============================================================================
# Copyright 2003-2009 Kitware, Inc.
#
# Distributed under the OSI-approved BSD License (the "License");
# see accompanying file Copyright.txt for details.
#
# This software is distributed WITHOUT ANY WARRANTY; without even the
# implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
# See the License for more information.
#=============================================================================
# (To distribute this file outside of CMake, substitute the full
#  License text for the above reference.)

# Modified by sfalexrog to accomodate Android builds. SDL3_LIBRARY
# will be a target which you'll want to link against. It will be built
# statically, mostly because that would save you from needing to
# also add SDL3MAIN_LIBRARY to your target, and will not require you to
# add SDL_android_main.c to your source sets.
# You can override this behavior by changing this file, but as far as
# I know having multiple shared libraries don't really give you any
# benefit on Android.
#
# Modified by brentfpage: SDL2->SDL3
# specified set(SDL_STATIC ON) in the if(ANDROID) block
# also, in app/src/main/cpp/CMakeLists.txt, instead of using the convenience
# variable ${SDL3_LIBRARY} in target_link_libraries(native-plasma ...
# ${SDL3_LIBRARY}$ ) I hard-coded the value SDL3-static, as in
# target_link_libraries(native-plasma ... SDL3-static )

if (ANDROID)
	set(_SDL_SOURCE_SEARCH_PATHS
        SDL
		deps/SDL
		external/SDL
		libs/SDL
	)

    # This is a hack to allow find_path to actually search for headers and sources
	set(_SDL_CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ${CMAKE_FIND_ROOT_PATH_MODE_INCLUDE})
	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE "NEVER")
    set(SDL_STATIC ON)

	find_path(_SDL3_SOURCE_DIR SDL.c
		HINTS
		${_SDL_SOURCE_SEARCH_PATHS}
		PATH_SUFFIXES src
		PATHS ${_SDL_SOURCE_SEARCH_PATHS}
	)

	set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ${_SDL_CMAKE_FIND_ROOT_PATH_MODE_INCLUDE})

	if (${_SDL3_SOURCE_DIR} STREQUAL "_SDL3_SOURCE_DIR-NOTFOUND")
		message(FATAL_ERROR "Could not find SDL3 source directory, cannot continue")
	endif()

	get_filename_component(_SDL3_PROJECT_DIR ${_SDL3_SOURCE_DIR} DIRECTORY)

	add_subdirectory(${_SDL3_PROJECT_DIR})

    # You can't directly specify a target in target_link_libraries
    # in ${}, but you can't specify a variable name without ${},
    # so here's a little hack to make ${target_name} behave like
    # it should.
	set(SDL3_LIBRARY SDL3 SDL3main)
	set(SDL3_INCLUDE_DIR ${_SDL3_PROJECT_DIR}/include)
    

else()

    SET(SDL3_SEARCH_PATHS
        ~/Library/Frameworks
        /Library/Frameworks
        /usr/local
        /usr
        /sw # Fink
        /opt/local # DarwinPorts
        /opt/csw # Blastwave
        /opt
    )

    FIND_PATH(SDL3_INCLUDE_DIR SDL.h
    	HINTS
    	$ENV{SDL3DIR}
    	PATH_SUFFIXES include/SDL3 include
    	PATHS ${SDL3_SEARCH_PATHS}
    )

    FIND_LIBRARY(SDL3_LIBRARY_TEMP
    	NAMES SDL3
    	HINTS
    	$ENV{SDL3DIR}
    	PATH_SUFFIXES lib64 lib
    	PATHS ${SDL3_SEARCH_PATHS}
    )

    IF(NOT SDL3_BUILDING_LIBRARY)
    	IF(NOT ${SDL3_INCLUDE_DIR} MATCHES ".framework")
    		# Non-OS X framework versions expect you to also dynamically link to
    		# SDL3main. This is mainly for Windows and OS X. Other (Unix) platforms
    		# seem to provide SDL3main for compatibility even though they don't
    		# necessarily need it.
    		FIND_LIBRARY(SDL3MAIN_LIBRARY
    			NAMES SDL3main
    			HINTS
    			$ENV{SDL3DIR}
    			PATH_SUFFIXES lib64 lib
    			PATHS ${SDL3_SEARCH_PATHS}
    		)
    	ENDIF(NOT ${SDL3_INCLUDE_DIR} MATCHES ".framework")
    ENDIF(NOT SDL3_BUILDING_LIBRARY)

    # SDL3 may require threads on your system.
    # The Apple build may not need an explicit flag because one of the
    # frameworks may already provide it.
    # But for non-OSX systems, I will use the CMake Threads package.
    IF(NOT APPLE)
    	FIND_PACKAGE(Threads)
    ENDIF(NOT APPLE)

    # MinGW needs an additional library, mwindows
    # It's total link flags should look like -lmingw32 -lSDL3main -lSDL3 -lmwindows
    # (Actually on second look, I think it only needs one of the m* libraries.)
    IF(MINGW)
    	SET(MINGW32_LIBRARY mingw32 CACHE STRING "mwindows for MinGW")
    ENDIF(MINGW)

    IF(SDL3_LIBRARY_TEMP)
    	# For SDL3main
    	IF(NOT SDL3_BUILDING_LIBRARY)
    		IF(SDL3MAIN_LIBRARY)
    			SET(SDL3_LIBRARY_TEMP ${SDL3MAIN_LIBRARY} ${SDL3_LIBRARY_TEMP})
    		ENDIF(SDL3MAIN_LIBRARY)
    	ENDIF(NOT SDL3_BUILDING_LIBRARY)

    	# For OS X, SDL3 uses Cocoa as a backend so it must link to Cocoa.
    	# CMake doesn't display the -framework Cocoa string in the UI even
    	# though it actually is there if I modify a pre-used variable.
    	# I think it has something to do with the CACHE STRING.
    	# So I use a temporary variable until the end so I can set the
    	# "real" variable in one-shot.
    	IF(APPLE)
    		SET(SDL3_LIBRARY_TEMP ${SDL3_LIBRARY_TEMP} "-framework Cocoa")
    	ENDIF(APPLE)

    	# For threads, as mentioned Apple doesn't need this.
    	# In fact, there seems to be a problem if I used the Threads package
    	# and try using this line, so I'm just skipping it entirely for OS X.
    	IF(NOT APPLE)
    		SET(SDL3_LIBRARY_TEMP ${SDL3_LIBRARY_TEMP} ${CMAKE_THREAD_LIBS_INIT})
    	ENDIF(NOT APPLE)

    	# For MinGW library
    	IF(MINGW)
    		SET(SDL3_LIBRARY_TEMP ${MINGW32_LIBRARY} ${SDL3_LIBRARY_TEMP})
    	ENDIF(MINGW)

    	# Set the final string here so the GUI reflects the final state.
    	SET(SDL3_LIBRARY ${SDL3_LIBRARY_TEMP} CACHE STRING "Where the SDL3 Library can be found")
    	# Set the temp variable to INTERNAL so it is not seen in the CMake GUI
    	SET(SDL3_LIBRARY_TEMP "${SDL3_LIBRARY_TEMP}" CACHE INTERNAL "")
    ENDIF(SDL3_LIBRARY_TEMP)

    INCLUDE(FindPackageHandleStandardArgs)

    FIND_PACKAGE_HANDLE_STANDARD_ARGS(SDL3 REQUIRED_VARS SDL3_LIBRARY SDL3_INCLUDE_DIR)
endif()
