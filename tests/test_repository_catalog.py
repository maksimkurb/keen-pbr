import tempfile
import unittest
from pathlib import Path

from build_scripts.generate_repository_page import collect_catalog


class RepositoryCatalogTest(unittest.TestCase):
    def test_ubuntu_aliases_reuse_existing_debian_architectures(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root_dir = Path(temp_dir)
            (root_dir / "debian/bookworm/amd64").mkdir(parents=True)
            (root_dir / "debian/trixie/arm64").mkdir(parents=True)

            catalog = collect_catalog(root_dir, "https://repo.test/repository/stable")

        self.assertEqual(
            catalog["ubuntu"],
            [
                {
                    "version": "noble",
                    "arch": "amd64",
                    "sourceLine": (
                        "deb [signed-by=/usr/share/keyrings/keen-pbr-archive-keyring.asc] "
                        "https://repo.test/repository/stable/debian/bookworm/amd64 ./"
                    ),
                },
                {
                    "version": "resolute",
                    "arch": "arm64",
                    "sourceLine": (
                        "deb [signed-by=/usr/share/keyrings/keen-pbr-archive-keyring.asc] "
                        "https://repo.test/repository/stable/debian/trixie/arm64 ./"
                    ),
                },
            ],
        )


if __name__ == "__main__":
    unittest.main()
