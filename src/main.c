#include <signal.h>

#include "core/app.h"

int main(int argc, char **argv) {
    signal(SIGPIPE, SIG_IGN);
    return app_run(argc, argv);
}
