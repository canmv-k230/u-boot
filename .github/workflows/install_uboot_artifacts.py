#!/usr/bin/env python3
import os
import shutil
import argparse
import json
from pathlib import Path

INSTALL_MAP = {
    "k230_evb_spinand": ("k230_evb", "_spi_nand"),
}

# Files to install with target naming pattern
FILES_TO_COPY = {
    "u-boot.bin": "u-boot{suffix}.bin",
    "u-boot-spl.bin": "u-boot-spl{suffix}.bin",
    "u-boot-spl-fastboot.bin": "u-boot-spl-fastboot{suffix}.bin",
}

def install_artifacts(source_dir, target_dir, delete_after=False):
    source_dir = Path(os.path.expanduser(source_dir)).resolve()
    target_dir = Path(os.path.expanduser(target_dir)).resolve()
    target_dir.mkdir(parents=True, exist_ok=True)

    for board_dir in source_dir.iterdir():
        if not board_dir.is_dir():
            continue

        board_name = board_dir.name
        target_board_dir, suffix = INSTALL_MAP.get(board_name, (board_name, ""))
        output_path = target_dir / target_board_dir
        output_path.mkdir(parents=True, exist_ok=True)

        for src_name, pattern in FILES_TO_COPY.items():
            src_file = board_dir / src_name
            if not src_file.is_file():
                print(f"[WARN] Missing {src_name} in {board_name}")
                continue

            dest_name = pattern.format(suffix=suffix)
            dest_file = output_path / dest_name
            shutil.copy2(src_file, dest_file)
            print(f"[OK]   {src_file} -> {dest_file}")

def main():
    parser = argparse.ArgumentParser(description="Install U-Boot artifacts with custom mapping.")
    parser.add_argument("--source", default="~/artifacts", help="Source artifacts directory")
    parser.add_argument("--target", default="~/output_uboot", help="Target install directory")

    args = parser.parse_args()
    install_artifacts(args.source, args.target)

if __name__ == "__main__":
    main()
