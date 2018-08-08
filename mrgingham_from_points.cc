#include "mrgingham.hh"
#include "mrgingham_internal.h"
#include <stdio.h>
#include <getopt.h>

using namespace mrgingham;

static bool read_points( std::vector<Point>* points, const char* file )
{
    FILE* fp = fopen(file, "r");
    if( fp == NULL )
    {
        fprintf(stderr, "couldn't open '%s'\n", file);
        return false;
    }

    char* line = NULL;
    size_t n = 0;

    while(getline(&line, &n, fp) >= 0)
    {
        double x,y;
        int Nread = sscanf(line, "%lf %lf", &x, &y);
        if(Nread != 2)
            continue;

        Point pt( (int)( x * FIND_GRID_SCALE + 0.5 ),
                  (int)( y * FIND_GRID_SCALE + 0.5 ) );
        points->push_back(pt);
    }
    fclose(fp);
    free(line);
    return true;
}


int main(int argc, char* argv[])
{
    const char* usage =
        "Usage: %s [--debug] points.vnl\n"
        "\n"
        "Given a set of pre-detected points, this tool finds a chessboard grid, and returns\n"
        "the ordered coordinates of this grid on standard output. The pre-detected points\n"
        "can come from something like test_dump_chessboard_corners.\n";

    struct option opts[] = {
        { "help",              no_argument,       NULL, 'h' },
        { "debug",             no_argument,       NULL, 'd' },
        {}
    };


    bool        debug               = false;

    int opt;
    do
    {
        // "h" means -h does something
        opt = getopt_long(argc, argv, "h", opts, NULL);
        switch(opt)
        {
        case -1:
            break;

        case 'h':
            printf(usage, argv[0]);
            return 0;

        case 'd':
            debug = true;
            break;

        case '?':
            fprintf(stderr, "Unknown option\n");
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    } while( opt != -1 );

    if( optind != argc-1)
    {
        fprintf(stderr, "Need a single points-file on the cmdline\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }


    std::vector<Point> points;
    if( !read_points(&points, argv[argc-1]) )
        return 1;

    std::vector<PointDouble> points_out;
    bool result = find_grid_from_points(points_out, points, debug);

    printf("# x y\n");
    if( result )
    {
        for(int i=0; i<(int)points_out.size(); i++)
            printf("%f %f\n", points_out[i].x, points_out[i].y);
        return 0;
    }
    return 1;
}
