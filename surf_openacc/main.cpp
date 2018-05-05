#include "surflib.h"
// #include "kmeans.h"
#include <ctime>
#include <iostream>
#include <thread>
#include <unistd.h>
#include <queue>
#include <mutex>

std::mutex img_lock;

int THREAD_EXIT_FLAG = false;

int mainImage(int single_mem_cpy)
{
    // Declare Ipoints and other stuff
    IpVec ipts;
    // IplImage *img=cvLoadImage("../images/img1.jpg");
    IplImage *img=cvLoadImage("../images/1.png");
    

    // Detect and describe interest points in the image
    clock_t start = clock();
    surfDetDes(img, ipts, single_mem_cpy, false, 1, 4, 2, 0.0004f); 
    clock_t end = clock();

    std::cout<< "Found: " << ipts.size() << " interest points" << std::endl;
    std::cout<< "Took: " << float(end - start) / CLOCKS_PER_SEC    << " seconds" << std::endl;

    // Draw the detected points
    drawIpoints(img, ipts);
    
    // Display the result
    showImage(img);

    return 0;
}

//-------------------------------------------------------

void captureThread(CvCapture* capture_0, CvCapture* capture_1, IplImage** img_0, IplImage** img_1)
{
    while(!THREAD_EXIT_FLAG)
    {
        *img_0 = cvQueryFrame(capture_0);
        *img_1 = cvQueryFrame(capture_1);
    }
}

void stitchingThread(IpVec *ipts_0, IpVec *ipts_1, IplImage** img_0_ptr, IplImage** img_1_ptr, cv::Mat* stitched)
{   
    while(!THREAD_EXIT_FLAG)
    {
        if((*ipts_0).size() == 0 || (*ipts_1).size() == 0)
            continue;
        try
        {
            IpPairVec matches;
            clock_t start, end;

            start = clock();
            getMatches(*ipts_0, *ipts_1, matches);
            end = clock();
            std::cout<< "Matching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            start = clock();
            // cv::Mat warpped = getWarppedAcc(matches, *img_1_ptr);
            cv::Mat warpped = getCvWarpped(matches, *img_1_ptr);
            end = clock();
            std::cout<< "warpping took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            start = clock();
            *stitched = getCvStitch(*img_0_ptr, warpped);
            end = clock();
            std::cout<< "stitching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            start = clock();
            IplImage* display = cvCloneImage(&(IplImage)*stitched);
            drawFPS(display);
            cvShowImage("stitched", display);
            end = clock();
            std::cout<< "Imshow took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;            
            std::cout << "----------------------------------------------" << endl;
        }
        catch(cv::Exception& e)
        {
            continue;
        }
    }
}

void featureStitchThread(int single_mem_cpy, int blend_mode, IplImage **img_0, IplImage **img_1, cv::Mat **stitched_cpy)
{
    IpVec ipts_0, ipts_0_cpy;
    IpVec ipts_1, ipts_1_cpy;
    IplImage *img_0_ptr, *img_1_ptr;
    cv::Mat H, stitched, H_mean;
    IpPairVec matches;
    clock_t start, end;

    int H_count=0;

    while(!THREAD_EXIT_FLAG) 
    {
        if(*img_0 == NULL or *img_1 == NULL)
            continue;
        
        // Get pointer to point to the current image captured by the capture thread
        img_0_ptr = *img_0;
        img_1_ptr = *img_1;
        H_count++;

        try{
            // if(H_count % 10 == 1)
            // {
            surfDetDes(img_0_ptr, ipts_0, single_mem_cpy, true, 4, 4, 2, 0.002f);        
            surfDetDes(img_1_ptr, ipts_1, single_mem_cpy, true, 4, 4, 2, 0.002f);        

            ipts_0_cpy = ipts_0;
            ipts_1_cpy = ipts_1;
            cout << ipts_0_cpy.size() << ", " << ipts_1_cpy.size() << endl;

            start = clock();
            getMatches(ipts_0, ipts_1, matches);
            end = clock();
            std::cout<< "Matching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            start = clock();
            H = findHom(matches);
            // }

            if(H_count == 1)
                H_mean = H;
            else
            {
                H_mean = 0.9*H_mean + 0.1*H;
            }

            cv::Mat warpped, mask2;
            std::vector<cv::Mat> warp_mask;

            if(blend_mode == 0)
                warpped = getWarppedAcc(img_1_ptr, H_mean);
            else
            {
                warp_mask = getWarppedAcc_blend(img_1_ptr, H_mean);
                mask2 = warp_mask[1];
                warpped = warp_mask[0];
            }
            end = clock();
            std::cout<< "warpping took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            start = clock();
            if(blend_mode == 0)
                stitched = getCvStitch(img_0_ptr, warpped);
            else
                stitched = getBlended(img_0_ptr, img_1_ptr, matches, warpped, mask2);
            
            // limit access to thread shared variable
            img_lock.lock();
            if(H_count == 1)
                *stitched_cpy = new cv::Mat(stitched);
            else
                **stitched_cpy = cv::Mat(stitched);
            img_lock.unlock();
            
            end = clock();
            std::cout<< "stitching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;
        }
        catch(cv::Exception& e)
        {
            continue;
        }

    }
}

void videoThread(cv::Mat** stitched_cpy)
{   
    clock_t start, end;
    sleep(5);
    while(!THREAD_EXIT_FLAG)
    {
        try{
            start = clock();
            // IplImage* display = cvCloneImage(&(IplImage)(*stitched_cpy));
            // drawFPS(display);
            // cvShowImage("stitched", display);
            img_lock.lock();
            cv::imshow("stitched", **stitched_cpy);
            img_lock.unlock();
            end = clock();
            std::cout<< "Imshow took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;            
            std::cout << "----------------------------------------------" << endl;
        }
        catch(cv::Exception& e)
        {
            continue;
        }
    }
}

int mainVideo(int single_mem_cpy, int blend_mode)
{
    // Initialise capture device
    // CvCapture* capture = cvCaptureFromCAM( CV_CAP_ANY );
    CvCapture* capture_0 = cvCaptureFromCAM(0);
    CvCapture* capture_1 = cvCaptureFromCAM(1);

    cvSetCaptureProperty(capture_0, CV_CAP_PROP_FRAME_HEIGHT, 720);
    cvSetCaptureProperty(capture_0, CV_CAP_PROP_FRAME_WIDTH, 1280);
    cvSetCaptureProperty(capture_1, CV_CAP_PROP_FRAME_HEIGHT, 720);
    cvSetCaptureProperty(capture_1, CV_CAP_PROP_FRAME_WIDTH, 1280);

    if(!capture_0) 
        error("No Capture Camera 0");
    if(!capture_1)
        error("No Capture Camera 1");

    // Initialise video writer
    //cv::VideoWriter vw("c:\\out.avi", CV_FOURCC('D','I','V','X'),10,cvSize(320,240),1);
    //vw << img;

    // Create a window 
    // cvNamedWindow("Camera0", CV_WINDOW_AUTOSIZE );
    // cvNamedWindow("Camera1", CV_WINDOW_AUTOSIZE );
    cvNamedWindow("stitched", CV_WINDOW_AUTOSIZE );

    // Declare Ipoints and other stuff
    IpVec ipts_0, ipts_0_cpy;
    IpVec ipts_1, ipts_1_cpy;
    IplImage *img_0 = NULL;
    IplImage *img_1 = NULL;
    IplImage *img_0_ptr, *img_1_ptr;
    cv::Mat H, stitched, *stitched_cpy, H_mean;
    IpPairVec matches;
    clock_t start, end;

    int H_count=0;

    std::thread t1(captureThread, capture_0, capture_1, &img_0, &img_1);
    // std::thread t2(stitchingThread, &ipts_0_cpy, &ipts_1_cpy, &img_0_ptr, &img_1_ptr, &stitched);
    // std::thread t3(videoThread, &stitched_cpy);

    sleep(1);
    std::thread t3(featureStitchThread, single_mem_cpy, blend_mode, &img_0, &img_1, &stitched_cpy);

    sleep(2);
    // Main capture loop
    while(1) 
    {
        if(img_0 == NULL or img_1 == NULL)
            continue;
        
        // Get pointer to point to the current image captured by the capture thread
        img_0_ptr = img_0;
        img_1_ptr = img_1;
        H_count++;

        try{
            // if(H_count % 10 == 1)
            // {
            //     surfDetDes(img_0_ptr, ipts_0, single_mem_cpy, true, 4, 4, 2, 0.002f);        
            //     surfDetDes(img_1_ptr, ipts_1, single_mem_cpy, true, 4, 4, 2, 0.002f);        

            //     ipts_0_cpy = ipts_0;
            //     ipts_1_cpy = ipts_1;
            //     cout << ipts_0_cpy.size() << ", " << ipts_1_cpy.size() << endl;

            //     start = clock();
            //     getMatches(ipts_0, ipts_1, matches);
            //     end = clock();
            //     std::cout<< "Matching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            //     start = clock();
            //     H = findHom(matches);
            // // }

            // if(H_count == 1)
            //     H_mean = H;
            // else
            // {
            //     H_mean = 0.9*H_mean + 0.1*H;
            // }

            
            // cv::Mat warpped = getWarppedAcc(img_1, H_mean);
            // // cv::Mat warpped = getWarpped(matches, img_1);
            // end = clock();
            // std::cout<< "warpping took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

            // start = clock();
            // stitched = getCvStitch(img_0, warpped);
            // img_lock.lock();
            // stitched_cpy = new cv::Mat(stitched);
            // img_lock.unlock();
            // end = clock();
            // std::cout<< "stitching took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;
            
            start = clock();
            img_lock.lock();
            IplImage* display = cvCloneImage(&(IplImage)(*stitched_cpy));
            drawFPS(display);
            cvShowImage("stitched", display);
            cvReleaseImage(&display);
            // cv::imshow("stitched", *stitched_cpy);
            img_lock.unlock();
            end = clock();
            std::cout<< "Imshow took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;            
            std::cout << "----------------------------------------------" << endl;
        }
        catch(cv::Exception& e)
        {
            continue;
        }

        // If ESC key pressed exit loop
        if( (cvWaitKey(10) & 255) == 27 ) break;
    }

    THREAD_EXIT_FLAG = true;
    cvReleaseCapture(&capture_0);
    cvReleaseCapture(&capture_1);
    cvDestroyWindow("Camera0");
    cvDestroyWindow("Camera1");
    return 0;
}


int mainStaticMatch(int single_mem_cpy, int blend_mode)
{
    IplImage *img_0, *img_1;
    cv::Mat warpped, stitched, mask2;
    std::vector<cv::Mat> warp_mask;
    // img1 = cvLoadImage("../images/img1.jpg");
    // img2 = cvLoadImage("../images/img2.jpg");
    img_0 = cvLoadImage("../images/web0.jpg");
    img_1 = cvLoadImage("../images/web1.jpg");

    // CvCapture* capture_0 = cvCaptureFromCAM(0);
    // CvCapture* capture_1 = cvCaptureFromCAM(1);

    // img1 = cvQueryFrame(capture_1);
    // img2 = cvQueryFrame(capture_0);

    clock_t start = clock();
    IpVec ipts1, ipts2;
    surfDetDes(img_0,ipts1,single_mem_cpy,false,4,4,2,0.0001f);
    surfDetDes(img_1,ipts2,single_mem_cpy,false,4,4,2,0.0001f);
    clock_t end = clock();
    std::cout<< "Took: " << float(end - start) / CLOCKS_PER_SEC    << " seconds to find featurs in 2 pics" << std::endl;


    IpPairVec matches;
    getMatches(ipts1,ipts2,matches);
    cv::Mat H = findHom(matches);

    if(blend_mode == 0)
        warpped = getWarppedAcc(img_1, H);
    else
    {
        warp_mask = getWarppedAcc_blend(img_1, H);
        mask2 = warp_mask[1];
        warpped = warp_mask[0];
    }
    end = clock();
    std::cout<< "warpping took: " << float(end - start) / CLOCKS_PER_SEC << std::endl;

    start = clock();
    if(blend_mode == 0)
        stitched = getCvStitch(img_0, warpped);
    else
        stitched = getBlended(img_0, img_1, matches, warpped, mask2);
    
    cvNamedWindow("stitched", CV_WINDOW_AUTOSIZE );
    cv::imshow("stitched", stitched);

    // for (unsigned int i = 0; i < matches.size(); ++i)
    // {
    //     drawPoint(img1,matches[i].first);
    //     drawPoint(img2,matches[i].second);
    
    //     const int & w = img1->width;
    //     cvLine(img1,cvPoint(matches[i].first.x,matches[i].first.y),cvPoint(matches[i].second.x+w,matches[i].second.y), cvScalar(255,255,255),1);
    //     cvLine(img2,cvPoint(matches[i].first.x-w,matches[i].first.y),cvPoint(matches[i].second.x,matches[i].second.y), cvScalar(255,255,255),1);
    // }

    // std::cout<< "Matches: " << matches.size();

    // cvNamedWindow("1", CV_WINDOW_AUTOSIZE );
    // cvNamedWindow("2", CV_WINDOW_AUTOSIZE );
    // cvShowImage("1", img1);
    // cvShowImage("2",img2);
    cvWaitKey(0);

    return 0;
}


//-------------------------------------------------------

int main(int argc, char* argv[]) 
{
    // single image SURF
    if(atoi(argv[1]) == 0)
        return mainImage(atoi(argv[2]));

    // show match between SURF
    if(atoi(argv[1]) == 1)
        return mainStaticMatch(atoi(argv[2]), atoi(argv[3]));

    // SURF on webcam
    if(atoi(argv[1]) == 2)
        return mainVideo(atoi(argv[2]), atoi(argv[3]));

}