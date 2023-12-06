#include <iostream>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>
#include "opencv2/opencv.hpp"
#include "dxl.hpp"
using namespace std;
using namespace cv;
bool ctrl_c_pressed = false;
void ctrlc_handler(int){ ctrl_c_pressed = true; }

int main() {
    string src = "nvarguscamerasrc sensor-id=0 ! video/x-raw(memory:NVMM), width=(int)640, height=(int)360, format=(string)NV12 ! nvvidconv flip-method=0 ! video/x-raw, width=(int)640, height=(int)360, format=(string)BGRx ! videoconvert ! video/x-raw, format=(string)BGR ! appsink"; 
    //string src = "lanefollow_100rpm_cw.mp4";
    
    VideoCapture cap(src,CAP_GSTREAMER);
    if (!cap.isOpened()){ cout << "Camera error" << endl; return -1; }
    
    string dst1 = "appsrc ! videoconvert ! video/x-raw, format=BGRx ! nvvidconv ! nvv4l2h264enc insert-sps-pps=true ! h264parse ! rtph264pay pt=96 ! udpsink host=203.234.58.168 port=8001 sync=false";
    string dst2 = "appsrc ! videoconvert ! video/x-raw, format=BGRx ! nvvidconv ! nvv4l2h264enc insert-sps-pps=true ! h264parse ! rtph264pay pt=96 ! udpsink host=203.234.58.168 port=8002 sync=false";
    
    VideoWriter writer1(dst1, 0, (double)30, Size(640, 360), true);
    if (!writer1.isOpened()) { cerr << "Writer open failed!" << endl; return -1;}

    VideoWriter writer2(dst2, 0, (double)30, Size(640, 90), true);
    if (!writer2.isOpened()) { cerr << "Writer open failed!" << endl; return -1;}                                      // 동영상 파일 불러오기
    
    if (!cap.isOpened()) { cerr << "Error opening camera." << endl; return -1; }    // 만약 동영상 파일이 없으면 에러
    
    Mat frame, black, dst, result_img;                                                // Mat 객체 생성
    Mat labels, stats, centroids;                                          // connectedComponentsWithStats을 생성하기 위해 필요한 객체
    int vel1 = 0,vel2 = 0,error=0;
    Dxl mx;    
    
    signal(SIGINT, ctrlc_handler);
    if(!mx.open()) { cout << "dynamixel open error"<<endl; return -1; }

    bool mode =false;
    char stop;
    
    while (true) {
        double start = getTickCount();                                              // 반복문 시간 시작단계
        cap >> frame;                                                                 // cap의 영상을 frame에 삽입
        cvtColor(frame, black,COLOR_BGR2GRAY);                                       // 이미지 그레이 컬러로 변경
        dst = black(Rect(0, 270, 640, 90));                                         // 크기 조절한 로드 맵

        double desiredMean = 70;                                                    // 밝기 보정
        Scalar meanValue = mean(dst);                                               // 밝기 보정 함수 mean사용
        double b = desiredMean - meanValue.val[0];                                  // 밝기 평균을 계산해서 b에 넣기
        result_img = dst + b;                                                       // 밝기 계산해서 원본 파일에 넣기

        threshold(result_img, result_img, 128, 255, THRESH_BINARY);                 // 이진화 128이상이면 255로 변경

        int p_width = result_img.cols;                                              // 이미지의 너비
        int p_height = result_img.rows;                                             // 이미지의 높이
        
        int numLabels = connectedComponentsWithStats(result_img, labels, stats, centroids);// 레이블링
        cvtColor(result_img, result_img, COLOR_GRAY2BGR);                           // 이미지 컬러로 변경

        static Point Left_pos((p_width) / 4, p_height/2);                              // 주 로드를 잡을 기준점 변경되어야 함으로 static을 사용
        static Point Right_pos((p_width) / 4 * 3, p_height/2);

        Point LeftInteger;
        Point RightInteger;

        if(mx.kbhit()){
            stop = mx.getch();
            if(stop == 'q') break;
            else if(stop == 's') mode = true; 
        }
        for (int i = 1; i < numLabels; i++) {                                       // 각각의 좌표점과 박스 색깔
            int left = stats.at<int>(i, CC_STAT_LEFT);                              // X축의 좌표
            int top = stats.at<int>(i, CC_STAT_TOP);                                // Y축의 좌표 
            int width = stats.at<int>(i, CC_STAT_WIDTH);                            // 너비의 길이
            int height = stats.at<int>(i, CC_STAT_HEIGHT);                          // 높이의 길이

            double x = centroids.at<double>(i, 0);                                  // 객체의 중심점 X
            double y = centroids.at<double>(i, 1);                                  // 객체의 중심점 Y
            
            LeftInteger = Left_pos - Point(x, y);                                       // 주 로드의 기준점과 객체의 무게중심 원점의 점
            RightInteger = Right_pos - Point(x, y);

            int label = (stats.at<int>(i, CC_STAT_AREA));                           // 레이블 기준으로 객체 구분
            if (label < 100)continue;                                                // 레이블 기준이 50이하면 제거

            if (((LeftInteger.x > 0 && LeftInteger.x <= 100) || (LeftInteger.x <= 0 && LeftInteger.x >= -100)) && LeftInteger.y <= 50) {  // 객체의 중심점과 주 로드의 점 비교
                Left_pos = Point(x, y);                                                  // 객체의 점 이동
                rectangle(result_img, Point(left, top), Point(left + width, top + height), Scalar(0, 255, 0), 2); // 사각형
                circle(result_img, Point(static_cast<int>(x), static_cast<int>(y)), 3, Scalar(0, 0, 255), -1); // 원 (중심 좌표에 점 찍기)
            }
            if (((RightInteger.x > 0 && RightInteger.x <= 100) || (RightInteger.x <= 0 && RightInteger.x >= -100)) && RightInteger.y <= 50) {  // 객체의 중심점과 주 로드의 점 비교
                Right_pos = Point(x, y);                                                  // 객체의 점 이동
                rectangle(result_img, Point(left, top), Point(left + width, top + height), Scalar(0, 255, 0), 2); // 사각형
                circle(result_img, Point(static_cast<int>(x), static_cast<int>(y)), 3, Scalar(0, 0, 255), -1); // 원 (중심 좌표에 점 찍기)
            }
            line(result_img, Left_pos, Right_pos, Scalar(0, 0, 255), 2);
            circle(result_img, Point(Left_pos.x + ((Right_pos.x - Left_pos.x)/2) , Left_pos.y + ((Right_pos.y - Left_pos.y)/2)), 3, Scalar(0, 255, 0), -1);
            error = 320 - (Left_pos.x + ((Right_pos.x - Left_pos.x) / 2));
        }
        
        writer1 << frame;
        writer2 << result_img;
        
        vel1 = 200 - 0.4* error;
        vel2 = -(200 + 0.4* error);
        
        if (ctrl_c_pressed) break;
        if(mode) mx.setVelocity(vel1,vel2);                                           // 기본 영상과 로드 영상

        double end = getTickCount();                                                // 종료 시간 기록
        double elapsedTime = (end - start) / getTickFrequency();                    // 실행 시간 계산 (초 단위로 변환)
        cout << "error: " << error <<  " 실행 시간 : " << elapsedTime << " 초, mean : " << meanValue.val[0] << endl;// 결과 출력
        //if (waitKey(30) == 'q') break;                                               // 'q'를 누르면 종료
    }
    mx.close();
    cap.release();                                                                  // 영상의 재생이 끝나면 종료
    return 0;                                                                       // 반환
}