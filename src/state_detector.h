#pragma once
#include "session.h"

void state_detector_start(Session *s);
void state_detector_start_cwd(Session *s);
void state_detector_start_child(Session *s);
void state_detector_stop(Session *s);
