#!/usr/bin/env python3
"""
maze_client.py

Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Simple userspace client for the mazegen kernel module.
             Opens /proc/mazegen, reads the freshly generated ASCII
             maze, and prints it to the console.
"""

PROC_PATH = "/proc/mazegen"


def get_maze():
    with open(PROC_PATH, "r") as f:
        return f.read()


def main():
    try:
        maze = get_maze()
    except FileNotFoundError:
        print(f"{PROC_PATH} not found - is the maze_module kernel module loaded?")
        print("Try: sudo insmod maze_module.ko")
        return
    except PermissionError:
        print(f"Permission denied reading {PROC_PATH}. Try running with sudo.")
        return

    print(maze)


if __name__ == "__main__":
    main()
