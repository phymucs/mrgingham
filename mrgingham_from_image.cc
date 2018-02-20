#include "mrgingham.hh"
#include <stdio.h>
#include <getopt.h>
#include <glob.h>
#include <pthread.h>

#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>

using namespace mrgingham;

struct mrgingham_thread_context_t
{
    const glob_t* _glob;
    int           Njobs;
    bool          doclahe;
    int           blur_radius;
    bool          doblobs;
    int           image_pyramid_level;
} ctx;

static void* worker( void* _ijob )
{
    // Worker thread. Processes images from the glob. Writes point detections
    // back out on the other end.

    int ijob = *(int*)(&_ijob);

    cv::Ptr<cv::CLAHE> clahe;

    if(ctx.doclahe)
    {
        clahe = cv::createCLAHE();
        clahe->setClipLimit(8);
    }

    for(int i_image=ijob; i_image<(int)ctx._glob->gl_pathc; i_image += ctx.Njobs)
    {
        const char* filename = ctx._glob->gl_pathv[i_image];

        cv::Mat image = cv::imread(filename, CV_LOAD_IMAGE_GRAYSCALE);
        if( image.data == NULL )
        {
            fprintf(stderr, "Couldn't open image '%s'\n", filename);
            flockfile(stdout);
            {
                printf("## Couldn't open image '%s'\n", filename);
                printf("%s - -\n", filename);
            }
            funlockfile(stdout);
            break;
        }

        if( ctx.doclahe )
            clahe->apply(image, image);
        if( ctx.blur_radius > 0 )
        {
            cv::blur( image, image,
                      cv::Size(1 + 2*ctx.blur_radius,
                               1 + 2*ctx.blur_radius));
        }

        std::vector<PointDouble> points_out;
        bool result;
        if(ctx.doblobs)
            result = find_circle_grid_from_image_array(points_out, image);
        else
            result = find_chessboard_from_image_array (points_out, image, ctx.image_pyramid_level);

        flockfile(stdout);
        {
            if( result )
            {
                for(int i=0; i<(int)points_out.size(); i++)
                    printf( "%s %f %f\n", filename,
                            points_out[i].x,
                            points_out[i].y);
            }
            else
                printf("%s - -\n", filename);
        }
        funlockfile(stdout);
    }

    return NULL;
}

int main(int argc, char* argv[])
{
    const char* usage =
        "Usage: %s [--clahe] [--blur radius]\n"
        "                   [--level l] --blobs|--chessboard imageglobs imageglobs ...\n"
        "\n"
        "  --blobs or --chessboard are required; these tell the tool what to do.\n"
        "\n"
        "  --clahe is optional: it will pre-process the image with an adaptive histogram\n"
        "  equalization step. This is useful if the calibration board has a lighting\n"
        "  gradient across it.\n"
        "\n"
        "  --blur radius   applies a blur (after --clahe, if given) to the image before\n"
        "  processing\n"
        "\n"
        "  --level l   applies a downsampling to the image before processing it (after\n"
        "  --clahe and --blur, if given) to the image before processing. Level 0 means\n"
        "  'use the original image'. Level > 0 means downsample by 2**level. Level < 0\n"
        "  means 'try several different levels until we find one that works. This is the'.\n"
        "  default.\n"
        "\n"
        "  --jobs N  will parallelize the processing N-ways. -j is a synonym. This is like\n"
        "  GNU make, except you're required to explicitly specify a job count.\n"
        "\n"
        "  The images are given as (multiple) globs.The output is a vnlog with columns\n"
        "  filename,x,y. All filenames matched in the glob will appear in the output.\n"
        "  Images for which no chessboard pattern was found appear as a single record\n"
        "  with null x and y.\n";

    struct option opts[] = {
        { "blobs",             no_argument,       NULL, 'B' },
        { "chessboard",        no_argument,       NULL, 'C' },
        { "blur",              required_argument, NULL, 'b' },
        { "clahe",             no_argument,       NULL, 'c' },
        { "level",             required_argument, NULL, 'l' },
        { "jobs",              required_argument, NULL, 'j' },
        { "help",              no_argument,       NULL, 'h' },
        {}
    };


    bool        have_doblobs        = false;
    bool        doblobs             = false; // to pacify compiler
    bool        doclahe             = false;
    int         blur_radius         = -1;
    int         image_pyramid_level = -1;
    int         jobs                = 1;

    int opt;
    do
    {
        // "h" means -h does something
        opt = getopt_long(argc, argv, "hj:", opts, NULL);
        switch(opt)
        {
        case -1:
            break;

        case 'h':
            printf(usage, argv[0]);
            return 0;

        case 'B':
        case 'C':
            if( have_doblobs )
            {
                fprintf(stderr, usage, argv[0]);
                return 1;

            }
            have_doblobs = true;
            doblobs      = (opt == 'B');
            break;

        case 'c':
            doclahe = true;
            break;

        case 'b':
            blur_radius = atoi(optarg);
            if(blur_radius <= 0)
            {
                fprintf(stderr, usage, argv[0]);
                return 1;

            }
            break;

        case 'l':
            image_pyramid_level = atoi(optarg);
            break;

        case 'j':
            jobs = atoi(optarg);
            break;

        case '?':
            fprintf(stderr, usage, argv[0]);
            return 1;
        }
    }  while( opt != -1 );

    if( !have_doblobs || optind < argc-1)
    {
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if( jobs <= 0 )
    {
        fprintf(stderr, "The job count must be a positive integer\n");
        fprintf(stderr, usage, argv[0]);
        return 1;
    }
    if( doblobs && image_pyramid_level >= 0)
    {
        fprintf(stderr, "warning: 'image_pyramid_level' only implemented for chessboards. Will be ignored\n");
    }

    glob_t _glob;
    int doappend = 0;
    for( int iopt_glob = optind; iopt_glob<argc; iopt_glob++ )
    {
        const char* imageglob = argv[argc-1];
        int globresult =
            glob(imageglob,
                 doappend |
                 GLOB_ERR | GLOB_MARK | GLOB_NOSORT | GLOB_TILDE_CHECK,
                 NULL, &_glob);
        if(globresult == GLOB_NOMATCH)
        {
            fprintf(stderr, "'%s' matched no files!\n", imageglob);
            return 1;
        }
        if(globresult != 0)
        {
            fprintf(stderr, "globbing '%s' failed!\n", imageglob);
            return 1;
        }

        doappend = GLOB_APPEND;
    }


    printf("# filename x y\n");

    // I'm done with the preliminaries. I now spawn the child threads. Note that
    // in this implementation it is important that these are THREADS and not a
    // fork. I want to make sure that the image output is atomic. To do that I
    // use flockfile(), and each child thread writes directly to stdout.
    // flockfile() does not work in a fork, but does work in a thread
    ctx._glob               = &_glob;
    ctx.Njobs               = jobs;
    ctx.doclahe             = doclahe;
    ctx.blur_radius         = blur_radius;
    ctx.doblobs             = doblobs;
    ctx.image_pyramid_level = image_pyramid_level;

    pthread_t thread[jobs];
    for(int i=0; i<jobs; i++)
        pthread_create(&thread[i], NULL, &worker, (void*)i);

    for(int i=0; i<jobs; i++)
        pthread_join(thread[i], NULL);

    return 0;
}
