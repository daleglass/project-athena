import hifi_utils
import hifi_android
import hashlib
import os
import platform
import re
import shutil
import tempfile
import json
import xml.etree.ElementTree as ET
import functools

print = functools.partial(print, flush=True)

# Encapsulates the vcpkg system
class QtDownloader:
    CMAKE_TEMPLATE = """
# this file auto-generated by hifi_qt.py
get_filename_component(QT_CMAKE_PREFIX_PATH "{}" ABSOLUTE CACHE)
get_filename_component(QT_CMAKE_PREFIX_PATH_UNCACHED "{}" ABSOLUTE)

# If the cached cmake toolchain path is different from the computed one, exit
if(NOT (QT_CMAKE_PREFIX_PATH_UNCACHED STREQUAL QT_CMAKE_PREFIX_PATH))
    message(FATAL_ERROR "QT_CMAKE_PREFIX_PATH has changed, please wipe the build directory and rerun cmake")
endif()
"""
    def __init__(self, args):
        self.args = args
        self.configFilePath = os.path.join(args.build_root, 'qt.cmake')
        self.assets_url = hifi_utils.readEnviromentVariableFromFile(args.build_root, 'EXTERNAL_BUILD_ASSETS')

        # OS dependent information
        system = platform.system()

        qt_found = False
        system_qt = False

        # Here we handle the 3 possible cases of dealing with Qt:
        if os.getenv('VIRCADIA_USE_SYSTEM_QT'):
            # 1. Using the system provided Qt. This is only recommended for Qt 5.15.0 and above,
            # as it includes a required fix on Linux.
            #
            # This path only works on Linux as neither Windows nor OSX ship Qt.

            if system != "Linux":
                raise Exception("Using the system Qt is only supported on Linux")

            self.path = None
            self.cmakePath = None

            qt_found = True
            system_qt = True
            print("Using system Qt")

        elif os.getenv('VIRCADIA_QT_PATH'):
            # 2. Using an user-provided directory.
            # VIRCADIA_QT_PATH must point to a directory with a Qt install in it.

            self.path = os.getenv('VIRCADIA_QT_PATH')
            self.fullPath = self.path
            self.cmakePath = os.path.join(self.fullPath, 'lib', 'cmake')

            qt_found = True
            print("Using Qt from " + self.fullPath)

        else:
            # 3. Using a pre-built Qt.
            #
            # This works somewhat differently from above, notice how path and fullPath are
            # used differently in this case.
            #
            # In the case of an user-provided directory, we just use the user-supplied directory.
            #
            # For a pre-built qt, however, we have to unpack it. The archive is required to contain
            # a qt5-install directory in it.

            self.path = os.path.expanduser("~/vircadia-files/qt")
            self.fullPath = os.path.join(self.path, 'qt5-install')
            self.cmakePath = os.path.join(self.fullPath, 'lib', 'cmake')

            if (not os.path.isdir(self.path)):
                os.makedirs(self.path)

            qt_found = os.path.isdir(self.fullPath)
            print("Using a packaged Qt")


        if not system_qt:
            if qt_found:
                # Sanity check, ensure we have a good cmake directory
                qt5_dir = os.path.join(self.cmakePath, "Qt5")
                if not os.path.isdir(qt5_dir):
                    raise Exception("Failed to find Qt5 directory under " + self.cmakePath + ". There should be a " + qt5_dir)
                else:
                    print("Qt5 check passed, found " + qt5_dir)
                    
            # I'm not sure why this is needed. It's used by hifi_singleton.
            # Perhaps it stops multiple build processes from interferring?
            lockDir, lockName = os.path.split(self.path)
            lockName += '.lock'
            if not os.path.isdir(lockDir):
                os.makedirs(lockDir)

            self.lockFile = os.path.join(lockDir, lockName)

        if qt_found:
            print("Found pre-built Qt5")
            return

        if 'Windows' == system:
            self.qtUrl = self.assets_url + '/dependencies/vcpkg/qt5-install-5.12.3-windows3.tar.gz%3FversionId=5ADqP0M0j5ZfimUHrx4zJld6vYceHEsI'
        elif 'Darwin' == system:
            self.qtUrl = self.assets_url + '/dependencies/vcpkg/qt5-install-5.12.3-macos.tar.gz%3FversionId=bLAgnoJ8IMKpqv8NFDcAu8hsyQy3Rwwz'
        elif 'Linux' == system:
            import distro

            if distro.id() == 'ubuntu':
                u_major = int( distro.major_version() )
                u_minor = int( distro.minor_version() )

                if u_major == 16:
                    self.qtUrl = self.assets_url + '/dependencies/vcpkg/qt5-install-5.12.3-ubuntu-16.04-with-symbols.tar.gz'
                elif u_major == 18:
                    self.qtUrl = self.assets_url + '/dependencies/vcpkg/qt5-install-5.12.3-ubuntu-18.04.tar.gz'
                elif u_major == 19 and u_minor == 10:
                    self.qtUrl = self.assets_url + '/dependencies/vcpkg/qt5-install-5.12.6-ubuntu-19.10.tar.xz'
                elif u_major > 19:
                    print("")
                    print("We don't support " + distro.name(pretty=True) + " yet. Perhaps consider helping us out?")
                    self.showQtBuildInfo()
                    print("")
                    raise Exception('LINUX DISTRO IS NOT SUPPORTED YET!!!')
                else:
                    print("")
                    print("Sorry, " + distro.name(pretty=True) + " is old and won't be officially supported. Please consider upgrading.")
                    print("")
                    raise Exception('UNKNOWN LINUX DISTRO VERSION!!!')
            else:
                print("")
                print("Sorry, " + distro.name(pretty=True) + " is not supported. Please consider helping us out.")
                self.showQtBuildInfo()
                print("")
                raise Exception('UNKNOWN LINUX VERSION!!!')
        else:
            print("System      : " + platform.system())
            print("Architecture: " + platform.architecture())
            print("Machine     : " + platform.machine())
            raise Exception('UNKNOWN OPERATING SYSTEM!!!')

    def showQtBuildInfo(self):
        print("")
        print("It's also possible to build Qt for your distribution, please see the documentation at:")
        print("https://github.com/vircadia/vircadia/tree/master/tools/qt-builder")
        print("")
        print("Alternatively, you can try building against the system Qt by setting the VIRCADIA_USE_SYSTEM_QT environment variable.")
        print("You'll need to install the development packages, and to have Qt 5.15.0 or newer. ")

    def writeConfig(self):
        print("Writing cmake config to {}".format(self.configFilePath))
        # Write out the configuration for use by CMake
        cmakeConfig = QtDownloader.CMAKE_TEMPLATE.format(self.cmakePath, self.cmakePath).replace('\\', '/')
        with open(self.configFilePath, 'w') as f:
            f.write(cmakeConfig)

    def installQt(self):
        if not os.path.isdir(self.fullPath):
            print ('Downloading Qt from AWS')
            print('Extracting ' + self.qtUrl + ' to ' + self.path)
            hifi_utils.downloadAndExtract(self.qtUrl, self.path)
        else:
            print ('Qt has already been downloaded')
