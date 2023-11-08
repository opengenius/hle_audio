#pragma once

#include "internal_jobs_types.h"

static void launch(const jobs_t& jobs_obj, hlea_job_t job) {
    jobs_obj.vt->launch(jobs_obj.udata, job);
}
