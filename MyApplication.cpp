
#include "Utilities.h"

/*The cushion to cushion size of the table is 1434mm by 683mm and 
so the transformed table image has been set as 683 rows by 1434 columns.  
Each ball is 43mm in diameter.*/
void MyApplication()
{
	char* file_location = "Media/Pool/";
	char* pool_files[] = {
		"AllBalls.jpeg",
		"Table1.jpeg",
		"Table2.jpeg",
		"Table3.jpeg",
		"Table4.jpeg",
		"Table5.jpeg",
		"Table6.jpeg"
	};

	// Load images
	int number_of_images = sizeof(pool_files) / sizeof(pool_files[0]);
	Mat* pool_image = new Mat[number_of_images];
	//Mat* new_pool_image = new Mat[number_of_images];
	for (int file_no = 0; (file_no < number_of_images); file_no++)
	{
		cout << pool_files[file_no] << endl;
		string filename(file_location);
		filename.append(pool_files[file_no]);
		pool_image[file_no] = imread(filename, -1);

		if (pool_image[file_no].empty())
		{
			cout << "Could not open " << pool_image[file_no] << endl;
		}
		imshow(pool_files[file_no], pool_image[file_no]);

		char pause = cv::waitKey();  // This introduces a delay between frame.  The length of the delay/number can be reduced.
	}

	if (!pool_image[0].empty())
	{
		Mat grey;
		cvtColor(pool_image[0], grey, COLOR_BGR2GRAY);

		vector<Vec3f> circles;
		HoughCircles(grey, circles, HOUGH_GRADIENT, 1.0, 43.0,
			100.0, 20.0, 20, 23);

		Mat detected_balls = pool_image[0].clone();
		for (const Vec3f& ball : circles)
		{
			Point centre(cvRound(ball[0]), cvRound(ball[1]));
			circle(detected_balls, centre, cvRound(ball[2]), Scalar(0, 255, 0), 2);
			circle(detected_balls, centre, 2, Scalar(0, 0, 255), FILLED);
		}
		imshow("AllBalls circle detection", detected_balls);
		waitKey();
	}

}

