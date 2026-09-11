/*
 * maze_module.c
 *
 * A Linux kernel module that generates a random ASCII maze
 * (recursive backtracker algorithm) every time /proc/mazegen
 * is opened and read.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/version.h>
#include <linux/ktime.h>

#define PROC_NAME   "mazegen"


#define CELL_COLS   39
#define CELL_ROWS   13
#define MAZE_COLS   (2 * CELL_COLS + 1)
#define MAZE_ROWS   (2 * CELL_ROWS + 1)
#define NUM_CELLS   (CELL_COLS * CELL_ROWS)

#define IDX(r, c)   ((r) * MAZE_COLS + (c))

static char  *maze_buffer;
static size_t maze_len;

static u32 rng_state;

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: A small xorshift32 pseudo-random number generator.
*/
static u32 my_rand(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Seeds the module's PRNG using the current kernel time.
*/
static void my_seed_rand(void)
{
    rng_state = (u32)(ktime_get_real_ns() & 0xFFFFFFFFu);
    if (rng_state == 0)
        rng_state = 0xDEADBEEF; /* xorshift can't be seeded with 0 */
}

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Generates a random maze using an iterative recursive
             backtracker algorithm. Starts at cell (0,0), and on
             each step looks at the current cell's unvisited
             neighbors; if any exist, picks one at random, knocks
             down the wall between the two cells, marks the new
             cell visited, and pushes it onto an explicit stack
             (recursion is avoided to keep kernel stack usage
             small). When a cell has no unvisited neighbors it is
             popped off the stack ("backtracking"). The result is
             written into the flat character grid pointed to by
             "grid", using '#' for walls and ' ' for open space.
             An opening is carved on the top-left (entrance) and
             bottom-right (exit) border of the maze.
*/
static void generate_maze(char *grid)
{
    int *stack;
    bool *visited;
    int stack_top = 0;
    int cur, r, c, i;

    static const int dr[4] = { -1, 1, 0, 0 };
    static const int dc[4] = { 0, 0, -1, 1 };

    memset(grid, '#', MAZE_ROWS * MAZE_COLS);

    stack = kmalloc_array(NUM_CELLS, sizeof(int), GFP_KERNEL);
    visited = kzalloc(NUM_CELLS * sizeof(bool), GFP_KERNEL);
    if (!stack || !visited) {
        kfree(stack);
        kfree(visited);
        return;
    }

    cur = 0; 
    visited[cur] = true;
    grid[IDX(1, 1)] = ' ';
    stack[stack_top++] = cur;

    while (stack_top > 0) {
        int candidates[4];
        int num_candidates = 0;

        cur = stack[stack_top - 1];
        r = cur / CELL_COLS;
        c = cur % CELL_COLS;

        for (i = 0; i < 4; i++) {
            int nr = r + dr[i];
            int nc = c + dc[i];

            if (nr < 0 || nr >= CELL_ROWS || nc < 0 || nc >= CELL_COLS)
                continue;
            if (visited[nr * CELL_COLS + nc])
                continue;
            candidates[num_candidates++] = i;
        }

        if (num_candidates > 0) {
            int choice = candidates[my_rand() % num_candidates];
            int nr = r + dr[choice];
            int nc = c + dc[choice];
            int wall_r = 2 * r + 1 + dr[choice];
            int wall_c = 2 * c + 1 + dc[choice];

            grid[IDX(wall_r, wall_c)] = ' ';
            grid[IDX(2 * nr + 1, 2 * nc + 1)] = ' ';
            visited[nr * CELL_COLS + nc] = true;
            stack[stack_top++] = nr * CELL_COLS + nc;
        } else {
            stack_top--; 
        }
    }

    /* Carve an entrance (top-left) and exit (bottom-right). */
    grid[IDX(0, 1)] = ' ';
    grid[IDX(MAZE_ROWS - 1, MAZE_COLS - 2)] = ' ';

    kfree(stack);
    kfree(visited);
}

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Called whenever a process reads from /proc/mazegen.
             When a new read session begins (ppos == 0) a brand
             new maze is generated and stored in maze_buffer. The
             contents of maze_buffer are then copied out to user
             space via simple_read_from_buffer(), which correctly
             handles partial reads and repeated calls until the
             whole maze has been delivered.
*/
static ssize_t my_maze_read(struct file *file, char __user *buf,
                             size_t count, loff_t *ppos)
{
    if (*ppos == 0) {
        char *grid;
        size_t pos = 0;
        int r;

        kfree(maze_buffer);
        maze_buffer = NULL;
        maze_len = 0;

        grid = kmalloc(MAZE_ROWS * MAZE_COLS, GFP_KERNEL);
        if (!grid)
            return -ENOMEM;

        generate_maze(grid);

        maze_buffer = kmalloc(MAZE_ROWS * (MAZE_COLS + 1) + 1, GFP_KERNEL);
        if (!maze_buffer) {
            kfree(grid);
            return -ENOMEM;
        }

        for (r = 0; r < MAZE_ROWS; r++) {
            memcpy(maze_buffer + pos, grid + IDX(r, 0), MAZE_COLS);
            pos += MAZE_COLS;
            maze_buffer[pos++] = '\n';
        }
        maze_buffer[pos] = '\0';
        maze_len = pos;

        kfree(grid);
    }

    return simple_read_from_buffer(buf, count, ppos, maze_buffer, maze_len);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 6, 0)
static const struct proc_ops maze_fops = {
    .proc_read = my_maze_read,
};
#else
static const struct file_operations maze_fops = {
    .owner = THIS_MODULE,
    .read  = my_maze_read,
};
#endif

static struct proc_dir_entry *maze_proc_entry;

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Module init function. Seeds the PRNG with the current
             kernel time and creates the /proc/mazegen entry that
             user-space programs read the generated maze from.
*/
static int __init my_maze_init(void)
{
    my_seed_rand();

    maze_proc_entry = proc_create(PROC_NAME, 0444, NULL, &maze_fops);
    if (!maze_proc_entry) {
        pr_err("mazegen: failed to create /proc/%s\n", PROC_NAME);
        return -ENOMEM;
    }

    pr_info("mazegen: module loaded, /proc/%s is ready\n", PROC_NAME);
    return 0;
}

/*Name: Rafael Torres de Andrade
Date: September 07, 2026
Description: Module exit function. Removes the /proc/mazegen entry
             and frees any maze buffer still allocated, cleaning up
             everything the module allocated at init/read time.
*/
static void __exit my_maze_exit(void)
{
    proc_remove(maze_proc_entry);
    kfree(maze_buffer);
    maze_buffer = NULL;
    pr_info("mazegen: module unloaded, /proc/%s removed\n", PROC_NAME);
}

module_init(my_maze_init);
module_exit(my_maze_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Student Name <student@example.com>");
MODULE_DESCRIPTION("Generates a random ASCII maze via a proc file, using an iterative recursive backtracker algorithm.");
