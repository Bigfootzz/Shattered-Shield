#include <iostream>
#include <errno.h>
#include "opencv2/highgui/highgui.hpp"
#include "opencv2/imgproc/imgproc.hpp"
#include <fcntl.h>
#include <assert.h>

#include "ipc.h"

using namespace cv;
using namespace std;

/**
 * Yellow: 18 29 26 93 184 228
 * Black:
 */

int snap = 0;

FILE *f = NULL;

void connect()
{
    printf("Connecting\n");
    f = fopen(RR_CAMERA_SOCKET_FILE, "w");
    if(!f) printf("ERROR %i %s\n", errno, strerror(errno));
    else
    {
        printf("CONNECTED\n");
    }
}

void uploadCameraState(CameraState *state)
{
//     printf("Writing\n");

    for(int i = 0; i < NUM_SEGMENTS; i++) printf("SEGMENT[%i]: (%f, %f, %f)\n", i, state->segment[i].x, state->segment[i].y, state->segment[i].z);

    size_t size = sizeof(*state);
    size_t res = fwrite(state, 1, size, f);
//     printf("RES %lu %lu\n", res, size);

    // Only send one to the pipe
    fflush(f);
}

struct Trackingpoint
{
    Moments moment;
    Point3f center;
    int segment;

    Trackingpoint() : moment(), center(), segment(-1) {}
    ~Trackingpoint() {}
};

int main( int argc, char** argv )
{
    VideoCapture cap(0); //capture the video from webcam

    if ( !cap.isOpened() )  // if not success, exit program
    {
        cout << "Cannot open the web cam" << endl;
        return -1;
    }

    namedWindow("Control", WINDOW_AUTOSIZE); //create a window called "Control"

    int iLowH = 26;
    int iHighH = 60;

    int iLowS = 100;
    int iHighS = 255;

    int iLowV = 128;
    int iHighV = 255;

    int iWhite = 255;

    int cannyTresh = 100;

    int shouldBlur = 1;

    createTrackbar("Paused", "Control", &snap, 1); //Hue (0 - 179)

    createTrackbar("White", "Control", &iWhite, 255); //Hue (0 - 179)

    createTrackbar("LowH", "Control", &iLowH, 179); //Hue (0 - 179)
    createTrackbar("HighH", "Control", &iHighH, 179);

    createTrackbar("LowS", "Control", &iLowS, 255); //Saturation (0 - 255)
    createTrackbar("HighS", "Control", &iHighS, 255);

    createTrackbar("LowV", "Control", &iLowV, 255);//Value (0 - 255)
    createTrackbar("HighV", "Control", &iHighV, 255);

    createTrackbar("CannyTresh", "Control", &cannyTresh, 255);

    createTrackbar("Blur", "Control", &shouldBlur, 10);

    int iLastX = -1;
    int iLastY = -1;

    //Capture a temporary image from the camera
    Mat imgTmp;
    cap.read(imgTmp);

    //Create a black image with the size as the camera output
    Mat imgLines = Mat::zeros( imgTmp.size(), CV_8UC3 );;


    Mat imgOriginal;
    while (true)
    {

        if(snap == 0)
        {
            bool bSuccess = cap.read(imgOriginal); // read a new frame from video

            if (!bSuccess) //if not success, break loop
            {
                cout << "Cannot read a frame from video stream" << endl;
                break;
            }
        }

        Mat imgHSV;

        cvtColor(imgOriginal, imgHSV, COLOR_BGR2HSV);

        if(shouldBlur) blur(imgHSV, imgHSV, Size(shouldBlur*3,shouldBlur*3) );

        Mat imgFilteredWhite;

        inRange(imgHSV, Scalar(0, 0, 0), Scalar(255, 255, iWhite), imgFilteredWhite); //Filter out white

        Mat imgFilteredColor;

        inRange(imgHSV, Scalar(iLowH, iLowS, iLowV), Scalar(iHighH, iHighS, iHighV), imgFilteredColor); //Threshold the image

        Mat imgFiltered;

        imgFilteredColor = imgFilteredColor & imgFilteredWhite;

        Canny(imgFilteredColor, imgFiltered, cannyTresh, cannyTresh*2, 3 );

        vector<vector<Point> > contours;
        vector<Vec4i> hierarchy;

        findContours(imgFiltered, contours, hierarchy, RETR_TREE, CHAIN_APPROX_SIMPLE, Point(0, 0) );

        vector<Trackingpoint> trackers( contours.size() );
        Mat drawing = Mat::zeros( imgFiltered.size(), CV_8UC3 );

        for( int i = 0; i < contours.size(); i++ )
        {
            //Reset segments
            trackers[i].segment = -1;

            // Get the moments
            trackers[i].moment  = moments( contours[i], false );

            // get the centers
            Moments &moment     = trackers[i].moment;
            trackers[i].center  = Point3f(
                floor((moment.m10 / moment.m00)*10.f)/10.f,
                floor((moment.m01 / moment.m00)*10.f)/10.f,
                floor(moment.m00*10)/100.f
            );

            Scalar color = Scalar( 255, 0, 0 );

            drawContours( drawing, contours, i, color, 2, 8, hierarchy, 0, Point() );

            color = Scalar(0, 255, 0);

            circle(
                drawing,
                Point2f(
                    trackers[i].center.x,
                    trackers[i].center.y
                ),
                trackers[i].center.z,
                color, -1, 8, 0
            );
        }

        imshow("White filter", imgFilteredWhite);
        imshow("Color filter", imgFilteredColor);
        imshow("Final filtered", imgFiltered);

        Scalar colors [NUM_SEGMENTS] = {
            Scalar( 255, 0, 0),
            Scalar( 255, 255, 0),
            Scalar( 255, 255, 255),
            Scalar( 0, 255, 255),
        };

        printf("SIZE: %lu\n", trackers.size());

        if(!f)
        {
            connect();
        }
        else if(trackers.size() > 0)
        {
            static CameraCaptureState captureState = {0};

            captureState.extrapolate();

            // Match points to segments
            struct BestScore {
                float score; int tracker;
                BestScore() : score(FP_INFINITE), tracker(-1) {}
            } bestScores[NUM_SEGMENTS];

            // n^2 ftw!
            for(int k = 0; k < NUM_SEGMENTS; k++)
            {
                for(int j = 0; j < trackers.size(); j++)
                {
                    Trackingpoint &point = trackers[j];

                    // Skip already determined points
                    if(point.segment != -1) continue;

                    for(int i = 0; i < NUM_SEGMENTS; i++)
                    {
                        float score = 0;

                        if(captureState.extraPolatedState.segment[i].x != 0 && captureState.extraPolatedState.segment[i].y != 0 && captureState.extraPolatedState.segment[i].z != 0)
                        {
                            float dx = captureState.extraPolatedState.segment[i].x - point.center.x;
                            float dy = captureState.extraPolatedState.segment[i].y - point.center.y;
                            float dz = captureState.extraPolatedState.segment[i].z - point.center.z;

                            score = dx * dx + dy * dy + dz * dz; // TODO: Perhaps weigh the direction?

                            // Avoid other points
                            float shortestDistance = FP_INFINITE;

                            for(int l = 0; l < NUM_SEGMENTS; l++)
                            {
                                float dx = trackers[bestScores[l].tracker].center.x - point.center.x;
                                float dy = trackers[bestScores[l].tracker].center.y - point.center.y;
                                float dz = trackers[bestScores[l].tracker].center.z - point.center.z;

                                shortestDistance = min(shortestDistance, dx * dx + dy *dy + dz * dz);
                            }

                            score += max(0.f, 1000.f - shortestDistance);
                        }

//                         printf("%i %i %i: %f %f\n", k, j, i, score, bestScores[i].score);

                        if(bestScores[i].score > score)
                        {
                            bestScores[i].score     = score;
                            bestScores[i].tracker   = j;
                        }
                    }
                }

                // Try to remove duplicates
                for(int i = 0; i < NUM_SEGMENTS; i++)
                {
                    int tracker = bestScores[i].tracker;

                    if(trackers[tracker].segment == i || trackers[tracker].segment == -1 || (bestScores[trackers[tracker].segment].score > bestScores[i].score))
                    {
                        trackers[tracker].segment = i;
                        bestScores[i].score = 0;
                    }
                    else
                    {
//                         printf("RESET %i\n", i);
                        bestScores[i].score = __INTMAX_MAX__;
                        bestScores[i].tracker = -1;
                    }
                }
            }

            // Clear segment positions
            for(int i = 0; i < NUM_SEGMENTS; i ++)
            {
                captureState.cameraState.segment[i].x = 0;
                captureState.cameraState.segment[i].y = 0;
                captureState.cameraState.segment[i].z = 0;
            }

            // Update segment positions
            for(int i = 0; i < trackers.size(); i++)
            {
                Trackingpoint &point = trackers[i];

                if(point.segment == -1) continue;

                captureState.cameraState.segment[point.segment].x = point.center.x;
                captureState.cameraState.segment[point.segment].y = point.center.y;
                captureState.cameraState.segment[point.segment].z = point.center.z;

                circle(imgOriginal, Point(point.center.x, point.center.y), 2, colors[point.segment], -1, 8, 0);
            }

            captureState.calculateDelta();

            uploadCameraState(&captureState.cameraState);
        }

        imshow("Original", imgOriginal);
        imshow("Track", drawing);

        if (waitKey(30) == 27) //wait for 'esc' key press for 30ms. If 'esc' key is pressed, break loop
        {
            cout << "esc key is pressed by user" << endl;
            break;
        }
    }

    return 0;
}
