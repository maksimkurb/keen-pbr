from conan import ConanFile
from conan.tools.meson import MesonToolchain


class KeenPbr3Conan(ConanFile):
    name = "keen-pbr3"
    version = "3.0.0"
    settings = "os", "compiler", "build_type", "arch"
    options = {
        "with_api": [True, False],
    }
    default_options = {
        "with_api": True,
    }
    generators = "PkgConfigDeps"

    def requirements(self):
        self.requires("libcurl/8.5.0")
        self.requires("nlohmann_json/3.11.3")
        self.requires("libnl/3.8.0")
        self.requires("mbedtls/3.5.0")
        if self.options.with_api:
            self.requires("cpp-httplib/0.14.0")

    def generate(self):
        tc = MesonToolchain(self)
        tc.generate()
