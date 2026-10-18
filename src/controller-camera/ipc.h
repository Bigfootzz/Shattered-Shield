#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#define NUM_SEGMENTS 8

#pragma pack(push, 1)
struct CameraSegment
{
    float x, y, z;
};

struct CameraState
{
    CameraSegment segment[NUM_SEGMENTS];
};
#pragma pack(pop)

struct CameraCaptureState
{
    CameraState cameraState;
    CameraState delta;
    CameraState extraPolatedState;

    void extrapolate()
    {
        for(int i = 0; i < NUM_SEGMENTS; i++)
        {
            extraPolatedState.segment[i].x = cameraState.segment[i].x + delta.segment[i].x;
            extraPolatedState.segment[i].y = cameraState.segment[i].y + delta.segment[i].y;
            extraPolatedState.segment[i].z = cameraState.segment[i].z + delta.segment[i].z;
        }
    }

    void calculateDelta()
    {
        for(int i = 0; i < NUM_SEGMENTS; i++)
        {
            // delta            = new                       - old
            delta.segment[i].x  = cameraState.segment[i].x  - (extraPolatedState.segment[i].x - delta.segment[i].x);
            delta.segment[i].y  = cameraState.segment[i].y  - (extraPolatedState.segment[i].y - delta.segment[i].y);
            delta.segment[i].z  = cameraState.segment[i].z  - (extraPolatedState.segment[i].z - delta.segment[i].z);
        }
    }
};

#define RR_CAMERA_SOCKET_FILE "cameraControllerSocket"
