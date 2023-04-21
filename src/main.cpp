#include "vstp.h"


static vstp_state_t vstp_state;


void setup()
{
    vstp_init(&vstp_state);
}

void loop()
{
    vstp_update(&vstp_state);
}
