# ServerDash

ServerDash is a full-screen, touch-friendly C++ terminal dashboard built with
FTXUI. It discovers Kubernetes nodes from the active kubeconfig context and
provides Resources, Network, Storage, Actions, and SSH views. Node cards wrap
to fit the terminal and the current node count.

## Build

Requirements: CMake 3.20 or newer, a C++17 compiler, Git, and network access
on the first configure (CMake fetches FTXUI v7.0.3). On Windows with GCC from
MSYS2 UCRT64, make sure `C:\msys64\ucrt64\bin` is on `PATH`.

```powershell
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build
.\build\serverdash.exe
```
Press `q` or Escape to exit.

## Kubernetes and node access

Run ServerDash on a host with `kubectl` installed and a working kubeconfig
context that can list cluster nodes. `kubectl top nodes` supplies CPU and memory
utilization; install metrics-server in the cluster to enable those values.

The dashboard uses the system `ssh` client for node uptime, temperature,
filesystem, and network details, as well as shutdown/reboot. Configure
passwordless SSH access for each Kubernetes node name in the dashboard user's
SSH config/keychain. Set `SERVERDASH_SSH_USER` if the SSH username differs from
the local account. The SSH user needs permission to read `/proc`, `/sys`, and
filesystem statistics. Shutdown and reboot require passwordless `sudo` for
`shutdown -h now` and `reboot`.

Network throughput is calculated from byte counters between refreshes, so it
appears after the second refresh. WAN ping is measured from each node to
`1.1.1.1`. A node's root filesystem usage is shown in Storage.

Wake-on-LAN uses the `wakeonlan` utility installed on the dashboard host.
Because Kubernetes does not publish node MAC addresses, add this annotation
for each target node:

```sh
kubectl annotate node <node-name> serverdash.io/mac-address=<aa:bb:cc:dd:ee:ff>
```

Actions require explicit confirmation. The SSH tab opens the system SSH
terminal temporarily and returns to the dashboard when the session ends.
Refresh with the on-screen control or `r`; use left/right arrows to change
tabs and up/down arrows (or the node controls) to select a node.
