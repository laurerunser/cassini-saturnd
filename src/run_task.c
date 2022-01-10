#include "run_task.h"

//convert un tableau de string en un tableau de char (pour la commande exec)
char **get_char_from_string(string **argv, uint32_t length){
    char **tab = malloc((length + 1)* sizeof(char*));
    is_malloc_error(tab);
    // add all the strings from argv
    for (int i = 0; i < length; i ++){
        tab[i] = malloc(argv[i]->length +1); // +1 for the \0 at the end of the char*
                                             // (no included in the struct string)
        is_malloc_error(tab[i]);
        strcpy(tab[i],argv[i]->s);
        strcpy(tab[i]+argv[i]->length, "\0");
    }
    // add NULL at the last index (for execvp)
    tab[length] = malloc(sizeof(char*));
    is_malloc_error(tab[length]);
    tab[length] = NULL;
    return tab;
}

/* Checks if the current time corresponds with the timing t */
bool is_correct_timing(struct timing* t){
    // get current time
    time_t rawtime;
    struct tm *local_t;
    time(&rawtime);
    local_t = localtime(&rawtime);
    int day = local_t->tm_wday;
    int h = local_t->tm_hour;
    int m = local_t->tm_min;

    // masks
    uint8_t days_m = t->daysofweek << day;
    uint32_t hours_m = t->hours << h;
    uint64_t minutes_m = t->minutes << m;

    // compare
    if (((t->daysofweek && days_m) == 1)
     && ((t->hours && hours_m) == 1)
     && ((t->minutes && minutes_m) == 1)) {
        return true;
    } else {
        return false;
    }
}

/* For the child process that will execute a task : duplicate stdout and stderr to the
   files at saturnd/tasks/<id>/stdout (resp stderr) */
void move_stdout_stderr(int id) {
    char *dir_path = get_directory_id_path(id);

    char* filename = "/stdout";
    char *out = get_file_path(dir_path, filename);

    filename = "/stderr";
    char *err = get_file_path(dir_path, filename);

    int fd1 = open(out, O_CREAT | O_RDWR, S_IRWXU);
    int fd2 = open(err, O_CREAT | O_RDWR, S_IRWXU);
    dup2(fd1,STDOUT_FILENO);
    dup2(fd2,STDERR_FILENO);

    free(dir_path);
    free(out);
    free(err);
}

void write_run_info(int64_t time, uint16_t exitcode, uint64_t taskid) {
    // append the time and exitcodes
    char *path_dir = get_directory_id_path(taskid);
    char *filename = "/runs";
    char *path_runs = get_file_path(path_dir, filename);
    int fd = open(path_runs, O_APPEND | O_CREAT, S_IRWXU);

    BYTE buf[sizeof(int64_t) + sizeof(uint16_t)];
    memcpy(buf, &time, sizeof(int64_t));
    memcpy(buf+sizeof(int64_t), &exitcode, sizeof(uint16_t));

    write(fd, buf, sizeof(int64_t) + sizeof(uint16_t));
    close(fd);

    // write the number of runs
    filename = "/nb_runs";
    char *path_nb = get_file_path(path_dir, filename);
    fd = open(path_nb, O_RDWR);
    uint32_t nb_runs;
    if (fd < 0) { // file didn't exist
        fd = open(path_nb, O_RDWR | O_CREAT, S_IRWXU);
        nb_runs = 1;
    } else { // file already existed
        read(fd, &nb_runs, sizeof(uint32_t));
        nb_runs += 1;
    }
     write(fd, &nb_runs, sizeof(uint32_t));
    close(fd);

    free(path_dir);
    free(path_runs);
    free(path_nb);
}

void run_one_task(s_task *task, uint64_t taskid) {
    int f = fork();
    if (f == -1) {
        perror("Fork error");
        exit(EXIT_FAILURE);
    } else if (f == 0) { // fork and execute the task
        move_stdout_stderr(taskid);
        char **tab = get_char_from_string(task->command->argv, task->command->argc);
        char *com = tab[0];
        execvp(com,tab);
    } else { // wait until task is finished to write execution time + return value
        int status; // to store the return status of the child
        uint16_t exitcode; // the value to write to the file
        int64_t time_of_execution = time(NULL); // current time
        waitpid(f, &status, 0);

        if (WIFEXITED(status)) { // child sent a return value
            exitcode = WEXITSTATUS(status);
        } else { // child didn't send a return value
            exitcode = 0xFFFF;
        }
        write_run_info(time_of_execution, exitcode, taskid);
    }
}

void run_tasks(s_task **tasks, uint64_t nb_tasks){
    for (int i = 0; i < nb_tasks; i++) {
        
        if(!(tasks[i]->is_removed)) {
            if (is_correct_timing(tasks[i]->t)) {
                int f = fork();
                if (f == -1) {
                    perror("Fork error");
                    exit(EXIT_FAILURE);
                } else if (f == 0) {
                    // child that will launch the task so that saturnd doesn't block
                    run_one_task(tasks[i], i);
                    exit(0);
                }
            }
        }
    }
}
