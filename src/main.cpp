#include <Eigen/Dense>
#include <Eigen/Sparse>
#include <stdio.h>
#include <opencv2/imgproc/imgproc.hpp>
#include <iostream>
#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <math.h>
#include <ORBextractor.h>
#include <BlobDirection.h>
#include <helpers.h>
#include <chrono> 
#include <numeric> 
#include <ORBmatcher.h>
#include <opencv2/core/eigen.hpp>

using namespace cv;
using namespace std;
using namespace std::chrono;


static void threshold_trackbar (int , void* )
{
    LOG("====Threshold trackbar value" << threshold_slider)
    cv::Mat current, previous, pre_previous;
    current = images[frame_slider];
    previous = images[frame_slider - 1];
    pre_previous = images[frame_slider - 2];


    Mat match_results_whole_image_orb;
    {
        auto start = high_resolution_clock::now();
        cv::cvtColor(current, current_greyscale, cv::COLOR_BGR2GRAY);
        ORBextractorLeft->operator()(current_greyscale, NULL, kp_cur, des_cur);

        cv::cvtColor(previous, previous_greyscale, cv::COLOR_BGR2GRAY);
        ORBextractorLeft->operator()(previous_greyscale, NULL, kp_prev, des_prev);
        time_extracing_orb_features = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
        cout << "ORB Extraction time*2: " << time_extracing_orb_features << endl;

        start = high_resolution_clock::now();
        auto bf = BFMatcher(cv::NORM_HAMMING, true);
        std::vector< DMatch > matches;
        bf.match(des_prev, des_cur, matches);

        drawMatches(previous, kp_prev, current, kp_cur, matches, match_results_whole_image_orb, Scalar::all(-1), Scalar::all(-1), std::vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        time_orb_brute_force_whole_image = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
        cout << "Calculating all matches from brute force: " <<  time_orb_brute_force_whole_image << endl;
        brute_force_whole_img_num_of_matches = matches.size();
        CHECK_IMAGE(match_results_whole_image_orb, false);
    }
    templates.clear();
    angle_per_blob.clear();
    list_of_match_results.clear();
    list_of_match_results_withcrosscheck.clear();

    time_dilation_total = 0;
    time_keypoint_filtering_total = 0;
    time_angle_calculation_total = 0;
    time_matching_with_mask_total = 0;
    time_matching_without_mask_total = 0;
    mask_num_of_matches = 0;
    crosscheck_num_of_matches = 0;

    LOG("=====START OF PROCESSING========");
    LOG("Processing three frames");
    
    
    auto start = high_resolution_clock::now();
    cv::Mat current_gray, prev_gray, preprev_gray;
    cv::Mat diff_img, cur, prev;
    cv::Mat diff_cur_prev;
    cv::Mat diff_prev_preprev;

    cvtColor(current, current_gray,CV_RGB2GRAY);
    cvtColor(previous, prev_gray,CV_RGB2GRAY);
    cvtColor(pre_previous, preprev_gray,CV_RGB2GRAY);
    subtract(current_gray, prev_gray, diff_cur_prev, noArray(), CV_8U); //TODO change back to CV_16S
    subtract(prev_gray, preprev_gray, diff_prev_preprev, noArray(), CV_8U);

    if(use_basic_thresholding) {
        start = high_resolution_clock::now();
        threshold(diff_cur_prev, cur, threshold_slider, THRESHOLD_VALUE_CUR, CV_THRESH_BINARY);
        threshold(diff_prev_preprev, prev, threshold_slider, THRESHOLD_VALUE_PREV, CV_THRESH_BINARY);
        cout << "Default Thresholding: " << duration_cast<microseconds>(high_resolution_clock::now() - start).count()
             << endl;
    }
    else {
        start = high_resolution_clock::now();
        cv::adaptiveThreshold(diff_cur_prev, cur, THRESHOLD_VALUE_CUR, ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY,
                              1 + 2 * adapt_neighboorhood, -adapt_constant);
        cv::adaptiveThreshold(diff_prev_preprev, prev, THRESHOLD_VALUE_PREV, ADAPTIVE_THRESH_MEAN_C, CV_THRESH_BINARY,
                              1 + 2 * adapt_neighboorhood, -adapt_constant);
        cout << "AdaptiveThresholding: " << duration_cast<microseconds>(high_resolution_clock::now() - start).count()
             << endl;
    }
    Eigen::Matrix<uchar, Eigen::Dynamic, Eigen::Dynamic> b1;
    Eigen::Matrix<uchar, Eigen::Dynamic, Eigen::Dynamic> b2;


    cv2eigen(cur, b1);
    cv2eigen(prev, b2);
    b1 += b2;
    eigen2cv(b1, diff_img);


    LOG("TIME taken for tfp: " << time_three_frame_differencing)

    Mat hsv_image = Mat::zeros(Size(diff_img.cols, diff_img.rows), CV_8UC3);


    start = high_resolution_clock::now();
    blextr = new BlobExtractor(diff_img);
    LOG("DownScaling");
    //blextr->Downscale();

    cv::Mat blob;
    cv::Rect blob_bb;
    LOG("Iterating over Blobs");

    templates.clear();
    list_of_match_results.clear();
    while(blextr->GetNextBlob(blob, blob_bb)){
        LOG("==K is " << blextr->num_of_blobs);

        templates.push_back(blob);

        cv::Mat match_results;
        vector<size_t> kp_cur_blob, kp_prev_blob;
        std::vector< DMatch > matches;

        start = high_resolution_clock::now();
        filter_keypoints_indeces(kp_cur, kp_cur_blob, THRESHOLD_VALUE_CUR, blob, blob_bb);
        filter_keypoints_indeces(kp_prev, kp_prev_blob, THRESHOLD_VALUE_PREV, blob, blob_bb);
/*        if (kp_cur_blob.empty() || kp_prev_blob.empty())
            continue;*/
        time_keypoint_filtering = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
        time_keypoint_filtering_total += time_keypoint_filtering;
        cout << "TIME Filtering keypoints: " << time_keypoint_filtering << endl;

        start = high_resolution_clock::now();
        cout << "Blob " << calculate_angle_by_com(blob) << endl;
        //CHECK_IMAGE(blob, true);
        angle_per_blob.push_back(calculate_angle_by_com(blob(blob_bb)));
        time_angle_calculation = duration_cast<microseconds>(high_resolution_clock::now() - start).count();
        time_angle_calculation_total = time_angle_calculation;
        cout << "TIME Blob angle calculation: " << time_angle_calculation << endl;


        update_hsv_image(hsv_image, angle_per_blob.back(), blob);
        for(size_t i : kp_prev_blob)
        {
            int bestDist = 256;
            int bestIdx2 = -1;
            cv::Mat desc1 = des_prev.row(i);
            for(size_t i2 : kp_cur_blob)
            {
                cv::Mat desc2 = des_cur.row(i2);
                const int dist = DescriptorDistance(desc1,desc2);
                if(dist<bestDist)
                {
                    bestDist=dist;
                    bestIdx2=i2;
                }
            }
            if(bestDist<=50) {
                matches.emplace_back(i, bestIdx2, bestDist);
            }
        }

        drawMatches(previous, kp_prev, current, kp_cur, matches, match_results);//, Scalar::all(-1), Scalar::all(-1), std::vector<char>(), cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        list_of_match_results.push_back(match_results.clone());
    }

    auto base_processing_time = time_blob_extraction + time_three_frame_differencing + time_dilation_total + time_keypoint_filtering_total;
    auto processing_time_mask_blobangles = base_processing_time + time_angle_calculation_total + time_matching_with_mask_total;
    auto processing_time_nomask_crosscheck = base_processing_time + time_matching_without_mask;
    LOG("=================BASE TIMINGS===============================")
    LOG("ORB feature extraction time:                                " << time_extracing_orb_features/2);
    LOG("Three Frame differencing:                                   " << time_three_frame_differencing);
    LOG("Dilation (total):                                           " << time_dilation_total);
    LOG("Filtering (total):                                          " << time_keypoint_filtering_total);
    LOG("Base time (three_frame + blob_extr + dilation + filtering): " << base_processing_time);
    LOG("Base time (three_frame + blob_extr + dilation + filtering): " << base_processing_time);
    LOG("=================ANGLE GUIDED SEARCH========================")
    LOG("Total processing time by using mask and blob_angles:        " << processing_time_mask_blobangles);
    LOG("Num of matches found                               :        " << mask_num_of_matches);
    LOG("Mean processing time per match:                             " << processing_time_mask_blobangles/((float)mask_num_of_matches));
    LOG("=================JUST CROSSCHECK============================")
    LOG("Total processing time by not using mask but crosscheck :    " << processing_time_nomask_crosscheck);
    LOG("Num of matches found                               :        " << crosscheck_num_of_matches);
    LOG("Mean processing time per match:                             " << processing_time_nomask_crosscheck/((float)crosscheck_num_of_matches));
    LOG("============================================================")
    LOG("ORB brute force on whole image :                            " << time_orb_brute_force_whole_image);
    LOG("Num of matches found                               :        " << brute_force_whole_img_num_of_matches);
    LOG("ORB brute force mean time per match :                       " << time_orb_brute_force_whole_image/((float)brute_force_whole_img_num_of_matches));
    std::cout << "Blob Number: " << templates.size() << endl;
    std::cout << "List of match results: " << list_of_match_results.size() << endl;

    //---------------WINDOW SHOWING ------------------------------//
    LOG("Window Showing")
    cv::Mat optical_flow_gt;
    calc_optical_flow_gt(previous, current, optical_flow_gt);
    CHECK_IMAGE(optical_flow_gt, false);
    cv::imshow("Diff", diff_img);
    cvtColor(hsv_image, hsv_image, COLOR_HSV2BGR);
    CHECK_IMAGE(hsv_image, false);

    //------------For video making
    /*/
    LOG("Video Making")
    cv::Mat video_temp;
    cv::Mat matArray[] = {pre_previous,
                        previous,
                        current};
    cv::hconcat(matArray, 3, video_temp);
    //CHECK_IMAGE(video_temp,true);
    cv::imwrite("the_three_color_images/" + to_string(frame_slider) + ".jpg", video_temp);
    
    cv::Mat matArray2[] = {tfp->diff_cur_prev, tfp->diff_prev_preprev};
    cv::hconcat(matArray2, 2, video_temp);
    cv::imwrite("the_two_diff_imgs/" + to_string(frame_slider) + ".jpg", video_temp);

    cv::imwrite("diff_img/" + to_string(frame_slider) + ".jpg", tfp->diff_img);
    cv::imwrite("diff_img_unfiltered_blobs/" + to_string(frame_slider) + ".jpg", blextr->unfiltered_blob_img);
    cv::imwrite("diff_img_filtered_blobs/" + to_string(frame_slider) + ".jpg", blextr->blob_img);
    cv::imwrite("hsv_image/" + to_string(frame_slider) + ".jpg", hsv_image);
    cv::imwrite("dense_hsv_image/" + to_string(frame_slider) + ".jpg", optical_flow_gt);

    //Drawing with full keypoints
    cv::Mat current_with_kp, prev_with_kp, blob_img_colored;
    cv::drawKeypoints(current, kp_cur, current_with_kp);
    cv::drawKeypoints(previous, kp_prev, prev_with_kp);
    cv::cvtColor(blextr->blob_img, blob_img_colored, cv::COLOR_GRAY2RGB);
    cv::Mat matArray3[] = {prev_with_kp,
                    blob_img_colored,
                    current_with_kp};
    cv::hconcat(matArray3, 3, video_temp);
    cv::imwrite("keypoint_images_with_blob_middle/" + to_string(frame_slider) + ".jpg", video_temp);
    cv::hconcat(prev_with_kp, current_with_kp, video_temp);
    cv::imwrite("petros" + to_string(frame_slider) + ".jpg", video_temp);

    //Drawing with reduced keypoints
    if(blextr->num_of_blobs > 0){
        vector<KeyPoint> kp_cur_filtered, kp_prev_filtered;
        cv::Mat temp;
        blextr->GetBlobDilated(0,temp,3);
        filter_keypoints(kp_cur, kp_cur_filtered, 190, temp);
        filter_keypoints(kp_prev, kp_prev_filtered, 105, temp);
        cv::cvtColor(temp, temp, cv::COLOR_GRAY2RGB);
        cv::Mat current_with_kp_filt, prev_with_kp_filt;
        cv::drawKeypoints(current, kp_cur_filtered, current_with_kp_filt);
        cv::drawKeypoints(previous, kp_prev_filtered, prev_with_kp_filt);
        cv::Mat matArray4[] = {prev_with_kp_filt,
                    temp,
                    current_with_kp_filt};
        cv::hconcat(matArray4, 3, video_temp);                
        cv::imwrite("keypoint_images_with_blob_middle_filtered/" + to_string(frame_slider) + ".jpg", video_temp);
    }

    cv::Mat matches_base;
    cv::hconcat(previous, current, matches_base);     
    cv::Mat matches_mask_total = matches_base.clone(), matches_cross_total = matches_base.clone();
    for(int i = 0 ; i<list_of_match_results_withcrosscheck.size(); i++){
        cv::Mat difference;
        cv::subtract(list_of_match_results_withcrosscheck[i], matches_base, difference, noArray(), CV_8S);
        cv::add(matches_mask_total, difference, matches_mask_total, noArray(), CV_8U);
        cv::imwrite("matches_drawn_per_blob_filtered_crosscheck/" + to_string(frame_slider) +"_" + to_string(i) + ".jpg", list_of_match_results_withcrosscheck[i]);
    }
    cv::imwrite("matches_filtered_crosscheck/" + to_string(frame_slider) + ".jpg", matches_mask_total);

    for(int i = 0; i<list_of_match_results.size() ; i++){
        cv::Mat difference;
        cv::subtract(list_of_match_results[i], matches_base, difference, noArray(), CV_8S);
        cv::add(matches_cross_total, difference, matches_cross_total, noArray(), CV_8U);

        cv::imwrite("matches_drawn_per_blob_filtered_mask/" + to_string(frame_slider) +"_" + to_string(i) + ".jpg", list_of_match_results[i]);
    }
    cv::imwrite("matches_filtered_mask/" + to_string(frame_slider) + ".jpg", matches_cross_total);

    cv::Mat final_part;
    cv::Mat optical_flow_subwindow;
    cv::Mat top_subwindow, middle_subwindow, bottom_subwindow;

    cv::hconcat(hsv_image, optical_flow_gt, optical_flow_subwindow);

    cv::Mat temp;

    cv::hconcat(diff_img, blextr->unfiltered_blob_img , temp);
    cv::cvtColor(temp, temp, cv::COLOR_GRAY2RGB);

    cv::hconcat(matches_base, temp, top_subwindow);
    cv::hconcat(match_results_whole_image_orb, optical_flow_subwindow, middle_subwindow);
    cv::hconcat(matches_mask_total, matches_cross_total, bottom_subwindow);



    cv::vconcat(top_subwindow, middle_subwindow, final_part);
    cv::vconcat(final_part, bottom_subwindow, final_part);

    cv::imwrite("final_part/" + to_string(frame_slider) + ".jpg", final_part);
    //*/
}

static void frame_trackbar ( int , void* )
{
    if (frame_slider < 2) {
        return;
    }

    std::cout << "===Frame trackbar with value: " << frame_slider << std::endl;
    threshold_trackbar(threshold_slider, 0);
    imshow("RGB_Image", images[frame_slider]);
}

int main(int argc, char** argv )
{
    cv::FileStorage fSettings("../TUM1.yaml", cv::FileStorage::READ);
    int nFeatures = fSettings["ORBextractor.nFeatures"];
    float fScaleFactor = fSettings["ORBextractor.scaleFactor"];
    int nLevels = fSettings["ORBextractor.nLevels"];
    int fIniThFAST = fSettings["ORBextractor.iniThFAST"];
    int fMinThFAST = fSettings["ORBextractor.minThFAST"];

    //ORB_SLAM2::ORBmatcher* temp = new ORB_SLAM2::ORBmatcher("../Vocabulary/ORBvoc.txt", "../TUM1.yaml" , "../rgb/");

    ORBextractorLeft = new ORB_SLAM2::ORBextractor(nFeatures, fScaleFactor, nLevels, fIniThFAST, fMinThFAST);

    std::cout << "OpenCV version : " << CV_VERSION << endl;
    std::cout << "[0,1] angle " << atan2(0, 1) * 180 / PI << endl;
    std::cout << "[1,0] angle " << atan2(1, 0) * 180 / PI << endl;
    std::cout << "[-1,0] angle " << atan2(-1, 0) * 180 / PI << endl;
    //String img_folder_path = argv[1];
    std::vector<String> fn;
    int scale_factor = 4;

    //glob("C:\\Users\\Stagakis\\Desktop\\rgbd_dataset_freiburg1_xyz\\rgb_short", fn, false);
    glob("../rgb_mine/*png", fn, false);
    images.reserve(fn.size());
    LOG("Loading images");
    for(int i = 0; i < fn.size(); i++){
        Mat img_frame = imread(fn[i]);
        //cv::resize(img_frame, img_frame, cv::Size(), 1.0/scale_factor, 1.0/scale_factor, CV_INTER_NN);

        cv::resize(img_frame, img_frame, cv::Size(img_frame.cols * 0.5, img_frame.rows * 0.5), 0, 0, CV_INTER_CUBIC);
        images.push_back(img_frame.clone()); 
    }
    LOG("Creating Windows");
    createWindowsAndTrackbars();
    LOG("Starting Mainloop");
    frame_trackbar(0, 0);
    while(1){
        int k = waitKey(0);
        //std::cout << k << endl;
        if (k == 97)
        {
            frame_slider--;
            setTrackbarPos("Frame", "Control", frame_slider);
            //frame_trackbar(frame_slider, 0);
        }

        if (k == 100)
        {
            frame_slider++;
            setTrackbarPos("Frame", "Control", frame_slider);
            //frame_trackbar(frame_slider, 0);
        }
        if(k == 27)
            break;
    }

}


void createWindowsAndTrackbars() {

    cv::namedWindow("RGB_Image", WINDOW_FREERATIO);
    moveWindow("RGB_Image", 640, 0);

    cv::namedWindow("Diff", WINDOW_FREERATIO);
    moveWindow("Diff", 0, 0);

    cv::namedWindow("Control", WINDOW_FREERATIO);
    resizeWindow("Control", 640, 480);
    moveWindow("Control", 1280, 0);

    cv::namedWindow("Templates", WINDOW_FREERATIO);
    //*//
    cv::createTrackbar("Template", "Templates", &template_value, 30, [](int, void*) -> void { 
        if (templates.size() ==0 ) return;
        if (template_value > templates.size() - 1) return;
        imshow("Templates", templates[template_value]); });
    //*/

    cv::namedWindow("List_of_matches", WINDOW_FREERATIO);
    cv::namedWindow("List_of_matches_withcrosscheck", WINDOW_FREERATIO);
    cv::createTrackbar("Match", "Control", &match_result_value, 30, 
        [](int, void*) -> void { 
            if(list_of_match_results.size() == 0) return;
            if(match_result_value > list_of_match_results.size() - 1) setTrackbarPos("Match", "Control", list_of_match_results.size() - 1);
            imshow("List_of_matches", list_of_match_results[match_result_value]);

            imshow("Templates", templates[match_result_value]);
            template_value = match_result_value;
            });
    setTrackbarPos("Match", "Control", match_result_value);

    cv::createTrackbar("Frame", "Control", &frame_slider, images.size() - 1, frame_trackbar);
    cv::createTrackbar("Adapt_Neigh(2*N+1)", "Control", &adapt_neighboorhood, 50, frame_trackbar);
    cv::createTrackbar("-Adapt_Const", "Control", &adapt_constant, 20, frame_trackbar);
    cv::createTrackbar("Threshold", "Control", &threshold_slider, 255, threshold_trackbar);
    cv::createTrackbar("BasicTheshold(0/1)", "Control", &use_basic_thresholding, 1, threshold_trackbar);
    cv::createTrackbar("Dilation(2*X+1)", "Control", &dilation_slider, 3, [](int, void*) -> void {threshold_trackbar(0,0); });
    cv::createTrackbar("Angle_tolerance(2*X)", "Control", &blob_angle_tolerance, 15, [](int, void*) -> void {threshold_trackbar(0,0); });

}

