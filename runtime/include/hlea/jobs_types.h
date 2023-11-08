#pragma once

struct hlea_job_t {
    void (*job_func)(void* udata);
    void* udata;
};

struct hlea_jobs_ti {
    void (*launch)(void* udata, hlea_job_t job);
};
