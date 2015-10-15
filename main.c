#include <stdlib.h>
#include <stdio.h>
#include "common.h"
#include "fprf.h"

int main(int argc, char ** argv)
{
    FILE * fpin = NULL;
    FILE * fpout = NULL;
    fprf_options_t options;

    options.c = 1;
    options.xfer_treshold = 8;
    options.xfer_check_interval = 100;

    if(argc < 2)
    {
        printf("usage: program infile [numproc [outfile]]\n");
        return 0;
    }

    if((fpin = fopen(argv[1], "r")) == NULL)
    {
        printf("error opening file %s\n", argv[1]);
        exit(1);
    }
    options.numproc = (argc > 2 ? atoi(argv[2]) : 1);
    options.c = (argc > 3 ? atof(argv[3]) : 1);

    if(argc > 4)
    {
        options.xfer_treshold = atoi(argv[4]);
    }
    if(argc > 5)
    {
        options.xfer_check_interval = atoi(argv[5]);
    }

    if((fpout = (argc > 5 ? fopen(argv[6], "w") : stdout)) == NULL)
    {
        printf("error opening file %s\n", argv[6]);
        exit(1);
    }


    fprintf(fpout, "initing with input file: %s\n", argv[1]);
    fprf_init(fpin, fpout, options);
    fprintf(fpout, "init success\n");
    fclose(fpin);

    int32 flow = fprf_run();
    fprintf(fpout, "flow: %ld\n", flow);

    fprf_output();

    fprf_uninit();

    if(argc > 5)
        fclose(fpout);

    return 0;
}

