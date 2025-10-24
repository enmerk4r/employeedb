#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>

#include "common.h"
#include "parse.h"

void list_employees(struct dbheader_t *dbhdr, struct employee_t *employees) {
    int i = 0;

    if (NULL == dbhdr) return STATUS_ERROR;
    if (NULL == employees) return STATUS_ERROR;

    for (i=0; i < dbhdr->count; i++) {
        printf("Employee %d\n", i);
        printf("\tName: %s\n", employees[i].name);
        printf("\tAddress: %s\n", employees[i].address);
        printf("\tHours: %d\n", employees[i].hours);
    }
}

int add_employee(struct dbheader_t *dbhdr, struct employee_t **employees, char *addstring) {

    if (NULL == dbhdr) return STATUS_ERROR;
    if (NULL == employees) return STATUS_ERROR;
    if (NULL == *employees) return STATUS_ERROR;
    if (NULL == addstring) return STATUS_ERROR;

    char *name = strtok(addstring, ",");
    if (NULL == name) return STATUS_ERROR;
    char *addr = strtok(NULL, ",");
    if (NULL == addr) return STATUS_ERROR;
    char *hours = strtok(NULL, ",");
    if (NULL == hours) return STATUS_ERROR;

    struct employee_t *e = *employees;
    e = realloc(e, sizeof(struct employee_t) * (dbhdr->count + 1));
    if (e == NULL){
        return STATUS_ERROR;
    }

    dbhdr->count++;

    strncpy(e[dbhdr->count-1].name, name, sizeof(e[dbhdr->count-1].name)-1);
    strncpy(e[dbhdr->count-1].address, addr, sizeof(e[dbhdr->count-1].address)-1);
    e[dbhdr->count-1].hours = atoi(hours);

    *employees = e;

    return STATUS_SUCCESS;
}

int remove_employee(struct dbheader_t *dbhdr, struct employee_t **employees, char *removename){
    
    if (NULL == dbhdr) return STATUS_ERROR;
    if (NULL == employees) return STATUS_ERROR;
    if (NULL == *employees) return STATUS_ERROR;
    if (NULL == removename) return STATUS_ERROR;

    unsigned short count = dbhdr->count;

    struct employee_t *e = *employees;
    
    int w = 0;                    // write index
    for (int r = 0; r < count; r++){  // read index
        if (strncmp(e[r].name, removename, NAME_LEN) != 0){
            if (w != r) e[w] = e[r];
            w++;
        }
    }

    dbhdr->count = w;

    if (w == 0){
        free(e);
        *employees = NULL;
        return STATUS_SUCCESS;
    }

    // Reallocate memory
    struct employee_t *tmp = realloc(e, w * sizeof *e);
    if (tmp != NULL){
        e = tmp;
    }

    *employees = e;

    return STATUS_SUCCESS;

}

int read_employees(int fd, struct dbheader_t *dbhdr, struct employee_t **employeesOut) {
    if (fd < 0){
        printf("Got a bad FD from the user\n");
        return STATUS_ERROR;
    }

    int count = dbhdr->count;
    struct employee_t *employees = calloc(count, sizeof(struct  employee_t));
    if (employees == -1){
        printf("Malloc failed\n");
        return STATUS_ERROR;
    }

    read(fd, employees, count * sizeof(struct employee_t));

    int i = 0;
    for (; i < count; i++){
        employees[i].hours = ntohl(employees[i].hours);
    }

    *employeesOut = employees;
    return STATUS_SUCCESS;

}

int update_hours(struct dbheader_t *dbhdr, struct employee_t *employees, char *hoursstring)
{
    if (NULL == dbhdr) return STATUS_ERROR;
    if (NULL == employees) return STATUS_ERROR;
    if (NULL == hoursstring) return STATUS_ERROR;

    char *name = strtok(hoursstring, ",");
    if (NULL == name) return STATUS_ERROR;
    char *hours = strtok(NULL, ",");
    if (NULL == hours) return STATUS_ERROR;

    for (int i = 0; i < dbhdr->count; i++){
        if (strncmp(employees[i].name, name, NAME_LEN) == 0){
            int old = employees[i].hours;
            employees[i].hours = atoi(hours);
            printf("Updated %s from %d hrs to %d hrs\n", employees[i].name, old, employees[i].hours);
        }
    }

    return STATUS_SUCCESS;
}

int output_file(int fd, struct dbheader_t *dbhdr, struct employee_t *employees) {
    if (fd < 0){
        printf("Got a bad FD from the user\n");
        return STATUS_ERROR;
    }

    int realcount = dbhdr->count;
    unsigned int new_size = sizeof(struct dbheader_t) + (sizeof(struct employee_t) * realcount);

    dbhdr->magic = htonl(dbhdr->magic);
    dbhdr->filesize = htonl(new_size);
    dbhdr->count = htons(dbhdr->count);
    dbhdr->version = htons(dbhdr->version);

    // move cursor to the start of the file
    lseek(fd, 0, SEEK_SET);

    write(fd, dbhdr, sizeof(struct dbheader_t));

    int i = 0;
    for (; i < realcount; i++){
        employees[i].hours = htonl(employees[i].hours);
        write(fd, &employees[i], sizeof(struct employee_t));
    }

    // shrink file to match header->filesize
    if (ftruncate(fd, new_size) == -1) {
        perror("ftruncate");
        return STATUS_ERROR;
    }

    return 0;
}	

int validate_db_header(int fd, struct dbheader_t **headerOut) {
    if (fd < 0) {
        printf("Got a bad FD from the user\n");
        return STATUS_ERROR;
    }

    struct dbheader_t *header = calloc(1, sizeof(struct dbheader_t));
    if (header == -1){
        printf("Malloc failed to create a db header\n");
        return STATUS_ERROR;
    }

    if (read(fd, header, sizeof(struct dbheader_t)) != sizeof(struct dbheader_t)){
        perror("read");
        free(header);
        return STATUS_ERROR;
    }

    header->version = ntohs(header->version);
    header->count = ntohs(header->count);
    header->magic = ntohl(header->magic);
    header->filesize = ntohl(header->filesize);

    if (header->version != 1){
        printf("Improper header version\n");
        free(header);
        return -1;
    }
    if (header->magic != HEADER_MAGIC){
        printf("Improper header magic\n");
        free(header);
        return -1;
    }
    struct stat dbstat = {0};
    fstat(fd, &dbstat);
    if (header->filesize != dbstat.st_size){
        printf("Corrupted database\n");
        free(header);
        return -1;
    }

    *headerOut = header;
    return STATUS_SUCCESS;

}

int create_db_header(struct dbheader_t **headerOut) {
	struct dbheader_t *header = calloc(1, sizeof(struct dbheader_t));
    if (header == -1){
        printf("Malloc failed to create db header\n");
        return STATUS_ERROR;
    }

    header->version = 0x1;
    header->count = 0;
    header->magic = HEADER_MAGIC;
    header->filesize = sizeof(struct dbheader_t);

    *headerOut = header;

    return STATUS_SUCCESS;

}


