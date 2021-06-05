from conans import ConanFile, CMake

class colite(ConanFile):
    name = "colite"
    version = "0.0.1"
    exports_sources = "include/*", "CMakeLists.txt", "tests/*"
    no_copy_source = True

    options = {
        "enable_testing": [True, False]
    }
    default_options = {
        "enable_testing": False
    }
    generators = "cmake"

    def requirements(self):
        print("Hello world {}!\n".format(self.options.enable_testing))
        if self.options.enable_testing == True:
            self.requires("gtest/cci.20210126")
            self.requires("folly/2021.05.31.00")

    def build(self):
        if self.options.enable_testing:
            cmake = CMake(self)
            cmake.configure()
            cmake.build()
            cmake.test()

    def package(self):
        self.copy("*.hpp")

    def package_id(self):
        self.info.header_only()
