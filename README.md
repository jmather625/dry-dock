# dry-dock

A simple Linux based container that can do basic networking and isolation.

## Features
### Process Resource Management Features
Uses cgroups to:
 - Limit CPU time
 - Memory
 - Number of processes in a container
- Container isolates itself with new namespaces for PID, mount, networking, ...
- Has its own little init process to clean up orphans

### Networking Features
- Container can be assigned an IP
- Processes running in container can connect to internet
- Can forward ports between container and host

### Filesystem
- Container launches in new root partition
- Full read-write access to container
- Remounts `/dev`, `/sys`, `/proc`, things like `ps` only display information from the current namespace

### User Features
- Start container without being root

### Miscellaneous
- This container has an excellent user experience
- Can save/load container images for different Linux distributions (you path inside the container might be messed up though)

## Instructions
You can create a container by pretty much copying all of the non-kernel files of a Linux distribution into a directory. On Arch Linux, you can just run `mkdir container_rootfs && sudo pacstrap container_rootfs`.

Before running the container, make sure the virtual network interface pair and forwarding for networking is all setup with `make network-setup`. You can undo these changes with `make network-teardown`. If you want the container to be able to contact the internet, make sure to change `eth0` in `networking/setup.sh` to whatever your internet connected interface is (ex: on my laptop this is `wlp3s0`).

To run an executable in our container, just run `make container` and then `sudo ./container container_dir executable`.

To run an executable in our container without having to be root, just run `make non-root-container` and then `sudo ./non-root-container container_dir executable`.

## Small Tests
You can test networking by starting up a container that executes `/bin/bash` and have it ping an IP address like `8.8.8.8`. Note, right now there are issue with domain name resolution, so if you get an error there try out an IP address.

You can test some of the resource limits by setting them, running `make test && cp fork_test container_dir/ && cp mem_test container_dir`, then running `./mem_test` or `./fork_test` in the container. These will just progressively allocate more memory or fork respectively (only a reasonable amount, so they should not crash most machines). If they try to use more resources than they were allowed, the cgroup settings should lead to them getting killed.

## Note:
The cgroup paths and veth pair are hardcoded, but there is no reason multiple containers could not run at once if these were changed for the different instances.
