g++ -o way way.cpp xdg-shell-protocol.o $(pkg-config --cflags --libs wayland-client wayland-egl egl glesv2)

I want to try another approach by interacting directly with the Linux Control Groups (cgroups v1 or v2) and the Mali Kernel Base (kbase) driver features. I assume, our devices have swap space or ZRAM.

Here is the plan
----------------

Step I:
1. Identify the PID of the background application
2. Locate its corresponding memory cgroup path. Say, /sys/fs/cgroup/memory/plugins/ or /sys/fs/cgroup/memory/apps/

Step II: Swapping Out Textures

1. Freeze the Application Process by using cgroups v1 freezer to avoid new memory allocation:

echo FROZEN > /sys/fs/cgroup/freezer/apps/[app_name]/freezer.state

2. Induce Cgroup Memory Pressure by aggressively lowering the memory limit of the paused app's cgroup.
echo 20000000 > /sys/fs/cgroup/memory/apps/[app_name]/memory.limit_in_bytes #20MB

Now Linux's virtual memory subsystem forces the Mali driver to purge all "evictable" buffers (unpinned textures, command caches, and discardable surfaces).
Any anonymous memory allocations or modified texture data that cannot be dropped outright are compressed and pushed into the system's ZRAM or swap space, completely freeing up physical GPU memory tables.

3. Clear Global Driver Pools (If available)
Driver may hold onto a pre-allocated pool of physical pages. Those pages can be force flushed by:

if [ -f /sys/kernel/debug/mali/mem_pool_trim ]; then
    echo 1 > /sys/kernel/debug/mali/mem_pool_trimfi

Step III: Swapping In Textures (On Resume)
1. Restore to original max limit (e.g., 500MB)
echo 524288000 > /sys/fs/cgroup/memory/apps/[app_name]/memory.limit_in_bytes

2. Thaw/unfreeze the Application
echo THAWED > /sys/fs/cgroup/freezer/apps/[app_name]/freezer.state

As soon as the application is thawed, the composition engine or the app's internal thread will submit a rendering job to the Mali hardware.
When the Mali GPU tries to read a texture that was evicted, a hardware page fault is raised.
The kbase kernel driver catches this fault, pulls the data back from ZRAM/swap into active physical memory, maps it to the GPU page tables, and seamlessly resumes rendering.

