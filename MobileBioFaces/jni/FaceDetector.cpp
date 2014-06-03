#include "FaceDetector.hpp"
#include <iostream>
#include <android/log.h>
using namespace std;


#define LOG_TAG "Test"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,LOG_TAG,__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,LOG_TAG,__VA_ARGS__)

FaceDetector::FaceDetector(int h, int w) {
	this->maxDetectNum = 20;
	this->detected_faces = 0;
	this->buffer = (unsigned char**)malloc(sizeof(unsigned char*) * 100);

	omp_set_num_threads(24);


	for(int i = 0; i < 100; i++) 
	{
		this->buffer[i] = (unsigned char*)malloc(sizeof(unsigned char) * h*w);
	}
}

FaceDetector::~FaceDetector(void) {
	for(int i = 0; i < 100; i++) 
	{
		free(this->buffer[i]);
	}
	free(this->buffer);
}

void FaceDetector::Detect(IplImage* pImg) {
	int after_mer;
	LOGI("In detection");
	Mat draw_point = _FaceDetection(pImg, after_mer);
	

	#pragma omp parallel for  
	for (int i = 0;i<after_mer;i++)
	{			   
		if (draw_point.at<float>(i,5)>15 /*&& area < FACE_AREA_MAX && area > FACE_AREA_MIN*/)
		{
			cvRectangle(pImg,cvPoint(draw_point.at<float>(i,1),draw_point.at<float>(i,0)),
			cvPoint(draw_point.at<float>(i,3),draw_point.at<float>(i,2)),cvScalar(0,0,255),2);	
			//LT_pos[detected_faces] = Point2f(draw_point.at<float>(i,1),draw_point.at<float>(i,0));
			//RB_pos[detected_faces] = Point2f(draw_point.at<float>(i,3),draw_point.at<float>(i,2));

			LT_pos.push_back(Point2f(draw_point.at<float>(i,1),draw_point.at<float>(i,0)));
			RB_pos.push_back(Point2f(draw_point.at<float>(i,3),draw_point.at<float>(i,2)));
			//faces.push_back(Rect(Point2f(draw_point.at<float>(i,1),draw_point.at<float>(i,0)),Point2f(draw_point.at<float>(i,3),draw_point.at<float>(i,2))));
			faces.push_back(draw_point.at<float>(i,1));
			faces.push_back(draw_point.at<float>(i,0));
			faces.push_back(draw_point.at<float>(i,3));
			faces.push_back(draw_point.at<float>(i,2));

			detected_faces++;
		}
	}

}

Mat FaceDetector::_FaceDetection(IplImage* pImg, int &after_mer) {

	IplImage* gray;
	vector<int> height(50);
	vector<int> width(50);
	vector<const unsigned char*> pPyramidImages(50);     //װ�ؽ�����ͼ��
	Mat detectedpoint = Mat(800,6,CV_32F);
	
	/***********************************************************������ͼ��************************************************/

	gray = cvCreateImage( cvSize(pImg->width,pImg->height), 8, 1 );//��Ϊ�Ҷ�ͼƬ

	LOGI("Ok befor transfer 2 gray");
	cvCvtColor(pImg,gray,CV_RGB2GRAY);
	LOGI("Ok after transfer 2 gray");
	/***********************************************detection*******************************************************/

	int numLevels = 1;


	float scale=1.2;
	int hit=0;//��⵽Ŀ��ĸ���

	CvSize p_size =cvGetSize(pImg);


	int window_row =p_size.height*scale;
	int window_col =p_size.width*scale;
	int k=0;
	int count2 = 0;

	for(;;)
	{    
		window_row = window_row/scale;
		window_col = window_col/scale;
		if (window_row<TrainedParams.objSize||window_col<TrainedParams.objSize)
		{
			break;
		}

	/*	for (int i=0;i<15;i++)
		{
			cout<<TrainedParams.pNumStageTrees[i]<<endl;
		}*/

		CvSize dst_cvsize;
		IplImage *dst=0;

		dst_cvsize.width=(int)( window_col); //��ͼƬ��СΪԭ����scale��
		dst_cvsize.height=(int)(window_row);
		dst=cvCreateImage(dst_cvsize,8,1);
		cvResize(gray,dst,CV_INTER_NN);
		Mat small_img(dst,0); 

		width[k]=window_col;
		height[k]= window_row;

		int count = 0; //��ÿһ���������װ�� pPyramidImages[k]�У�������һ�����
		int step = dst->height;
		
		for (int i = 0; i < width[k]; i++)
		{
			for (int j = 0; j < height[k]; j++)
			{
				buffer[count2][count ++ ] = (unsigned char) dst->imageData[j * dst->widthStep + i];
			}
		}
		pPyramidImages[k]=buffer[count2++];
		k++;
		numLevels++;
		cvReleaseImage(&dst);
	}

	LOGI("Ok after pyam");
	/*****************************************************������**********************************************************/


	// pre-determined offset of pixels in a subwindow
	vector<int> offset(TrainedParams.objSize * TrainedParams.objSize);
	for(int k = 0; k < numLevels-1; k++) // process each pyramid image
	{
		int h =height[k];
		int w =width[k];
		double factor = pow(scale,k);

		// determine the step of the sliding subwindow
		int winStep = 2;
		int m = min(h, w);
		if(factor > 2 && m < 240) winStep = 1;

		// calculate the offset values of each pixel in a subwindow
		for(int p1=0, p2=0, gap=h-TrainedParams.objSize, j=0; j < TrainedParams.objSize; j++) // column coordinate
		{
			for(int i = 0; i < TrainedParams.objSize; i++) // row coordinate
			{
				offset[p1++] = p2++;
			}

			p2 += gap;
		}

		int colMax = w - TrainedParams.objSize + 1;
		int rowMax = h - TrainedParams.objSize + 1;
		#pragma omp parallel for  // process each subwindow

		for(int c = 0; c < colMax; c += winStep) // slide in column
		{
			const unsigned char *pPixel = pPyramidImages[k] + c * h; 
			for(int r = 0; r < rowMax; r += winStep, pPixel += winStep) // slide in row
			{    

				int treeIndex = 0;
				double _score = 0;
				int s;
				
				for(s = 0; s < TrainedParams.numStages; s++) 
				{
					double fx = 0;

					// test each tree classifier
					for(int j = 0; j < TrainedParams.pNumStageTrees[s]; j++)
					{
						short int node = TrainedParams.pTreeRoot[treeIndex];

						// test the current tree classifier
						while(node > -1) // branch node
						{
							float p1 = (float) pPixel[offset[TrainedParams.pPoints1[node]]];//points Ϊ������������1*400�������е�λ�á�offsetΪ�ڴ�ͼƬ�н�һ��20*20�Ĵ��ڱ�Ϊ1*400ʱ�Ĳ�����
							float p2 = (float) pPixel[offset[TrainedParams.pPoints2[node]]];//pixel[]ָ��ƫ�Ƶ�λ��

							float fea;

							if(p1 == 0 && p2 == 0) fea = 0;
							else fea = (p1 - p2) / (p1 + p2);

							if(fea < TrainedParams.pCutpoint[node]) node = TrainedParams.pLeftChild[node];
							else node = TrainedParams.pRightChild[node];
						}

						// leaf node
						node = - node - 1;
						fx = fx + TrainedParams.pFit[node];
						treeIndex++;
					}

					fx = 1.0 / (1.0 + exp(-2.0*fx));
					if(fx < TrainedParams.pStageThreshold[s])  
						break; // negative samples
					_score = _score + fx;
				}
				
				if(s == TrainedParams.numStages) // a face detected
				{
					double _row = floor(r * factor) + 1;
					double _col = floor(c * factor) + 1;
					double _size = floor(TrainedParams.objSize * factor);                    
					

					#pragma omp critical // modify the record by a single thread
					{
						//����⵽Ŀ������걣���ھ�����
						detectedpoint.at<float>(hit,0)=_row;
						detectedpoint.at<float>(hit,1)=_col;
						detectedpoint.at<float>(hit,2)=_row+_size;
						detectedpoint.at<float>(hit,3)=_col+_size;
						detectedpoint.at<float>(hit,4)=_size;
						detectedpoint.at<float>(hit,5)=_score;

						hit++;
					}
					
				}
			}
		}
	} 

	LOGI("Ok after detection and before merge");
	/************************************************�ϲ���ⴰ��*********************************************************/


	Mat predicate;
	predicate=Mat::zeros(hit,1,CV_32F);
	float lable=1;

	for (int i=0;i<hit;i++)
	{

		for (int j=i+1;j<hit;j++)
		{
			float delta = 0.3 * min(detectedpoint.at<float>(i,4), detectedpoint.at<float>(j,4));//�趨�ϲ��ķ�Χ
			if (abs(detectedpoint.at<float>(i,0)-detectedpoint.at<float>(j,0))<delta&&abs(detectedpoint.at<float>(i,1)-detectedpoint.at<float>(j,1))<delta&&detectedpoint.at<float>(i,0)+detectedpoint.at<float>(i,4)-detectedpoint.at<float>(j,0)-detectedpoint.at<float>(j,4)<delta&&detectedpoint.at<float>(i,1)+detectedpoint.at<float>(i,4)-detectedpoint.at<float>(j,1)-detectedpoint.at<float>(j,4)<delta)
			{
				if (predicate.at<float>(i,0)==0)
				{
					predicate.at<float>(i,0)=lable;
					lable++;
				}
				if (predicate.at<float>(j,0)==0)
				{
					predicate.at<float>(j,0)=predicate.at<float>(i,0);
				}
				if (predicate.at<float>(j,0)!=0&&predicate.at<float>(i,0)==0)
				{
					predicate.at<float>(i,0)=predicate.at<float>(j,0);
				}

			}
		}
	}

	/*******************************************�ϲ����ڴ���***********************************************************/


	Mat rectspoint = Mat(hit,6,CV_32F);
	//rectspoint=cvCreateMat(hit,6,CV_32F);
	int numfaces=0;
	if (lable>1)
	{
		int after=lable-1;

		for (int numCandidates=1; numCandidates<lable; numCandidates++)
		{
			int num=0;
			int rectspoint1=0,rectspoint2=0,rectspoint3=0,rectspoint4=0,rec_score=0;

			#pragma omp parallel for
			for (int i=0;i<hit;i++)
			{
			#pragma omp critical
				if (predicate.at<float>(i,0)==numCandidates)
				{
					rectspoint1=rectspoint1+detectedpoint.at<float>(i,0);
					rectspoint2=rectspoint2+detectedpoint.at<float>(i,1);
					rectspoint3=rectspoint3+detectedpoint.at<float>(i,2);
					rectspoint4=rectspoint4+detectedpoint.at<float>(i,3);
					rec_score=rec_score+ detectedpoint.at<float>(i,5);
					num++;
				}
			#pragma omp critical
				if (predicate.at<float>(i,0)==0&&numCandidates==1)
				{
					rectspoint.at<float>(after,0)=detectedpoint.at<float>(i,0);
					rectspoint.at<float>(after,1)=detectedpoint.at<float>(i,1);
					rectspoint.at<float>(after,2)=detectedpoint.at<float>(i,2);
					rectspoint.at<float>(after,3)=detectedpoint.at<float>(i,3);
					rectspoint.at<float>(after,4)=detectedpoint.at<float>(i,4);
					rectspoint.at<float>(after,5)=detectedpoint.at<float>(i,5);
					after++;
				}

			}
			#pragma omp critical
			{
				rectspoint.at<float>(numCandidates-1,0)=rectspoint1/num;
				rectspoint.at<float>(numCandidates-1,1)=rectspoint2/num;
				rectspoint.at<float>(numCandidates-1,2)=rectspoint3/num;
				rectspoint.at<float>(numCandidates-1,3)=rectspoint4/num;
				rectspoint.at<float>(numCandidates-1,4)=abs(rectspoint.at<float>(numCandidates-1,0)-rectspoint.at<float>(numCandidates-1,2));//����Ĵ�С
				rectspoint.at<float>(numCandidates-1,5)=rec_score;
			}
			numfaces=numfaces+(num-1);
		}
	}


	if (lable==1)
	{
		int after=lable-1;

		#pragma omp parallel for
		for (int i=0;i<hit;i++)
		{
			#pragma omp critical
			if (predicate.at<float>(i,0)==0)
			{
				rectspoint.at<float>(after,0)=detectedpoint.at<float>(i,0);
				rectspoint.at<float>(after,1)=detectedpoint.at<float>(i,1);
				rectspoint.at<float>(after,2)=detectedpoint.at<float>(i,2);
				rectspoint.at<float>(after,3)=detectedpoint.at<float>(i,3);
				rectspoint.at<float>(after,4)=detectedpoint.at<float>(i,4);
				rectspoint.at<float>(after,5)=detectedpoint.at<float>(i,5);
				after++;
			}

		}
	}
	/****************************************************�ϲ���������**********************************************************/
	////�ҵ����Ժϲ��Ĵ���
	numfaces=hit-numfaces;
	predicate=Mat::zeros(numfaces,1,CV_32F);
	lable=1;

	for (int i=0;i<numfaces;i++)
	{
		for (int j=0;j<numfaces;j++)
		{
			float delta = floor(rectspoint.at<float>(j,4)*0.2);
			if (i == j)
			{
				continue;
			}

			if (rectspoint.at<float>(i,0) >= rectspoint.at<float>(j,0)-delta
				&& rectspoint.at<float>(i,1) >= rectspoint.at<float>(j,1)-delta
				&& rectspoint.at<float>(i,0)+rectspoint.at<float>(i,4) <= rectspoint.at<float>(j,0)+rectspoint.at<float>(j,4)+delta
				&& rectspoint.at<float>(i,1)+rectspoint.at<float>(i,4) <= rectspoint.at<float>(j,1)+rectspoint.at<float>(j,4)+delta)
			{
				if (predicate.at<float>(i,0)==0)
				{
					predicate.at<float>(i,0)=lable;
					lable++;
				}
				if (predicate.at<float>(j,0)==0)
				{
					predicate.at<float>(j,0)=predicate.at<float>(i,0);
				}
				if (predicate.at<float>(j,0)!=0)
				{
					predicate.at<float>(i,0)=predicate.at<float>(j,0);
				}
			}

		}
	}
	//////�ϲ���������///
	Mat draw_point = Mat(numfaces,6,CV_32F);
	//draw_point=cvCreateMat(numfaces,6,CV_32F);
	after_mer=0;
	if (lable==1)
	{
		lable=2;//��û�п��Ժϲ��Ĵ��ڣ��������ѭ��ֻѭ��һ�Σ�ֱ�ӽ�ǰ����ֵ�������ĵ�
	}
	for (int numCandidates=1; numCandidates<lable; numCandidates++)
	{
		int num=0;
		int rectspoint1=0,rectspoint2=0,rectspoint3=0,rectspoint4=0,rec_size=0,rec_score;

		#pragma omp parallel for
		for (int i=0;i<numfaces;i++)
		{
			if (predicate.at<float>(i,0)==numCandidates)
			{
				#pragma omp critical
				if (rectspoint.at<float>(i,4)>rec_size)
				{
					rectspoint1=rectspoint.at<float>(i,0);
					rectspoint2=rectspoint.at<float>(i,1);
					rectspoint3=rectspoint.at<float>(i,2);
					rectspoint4=rectspoint.at<float>(i,3);
					rec_size=rectspoint.at<float>(i,4);
					rec_score=rectspoint.at<float>(i,5);

				}
				num++;

			}
			#pragma omp critical
			if (predicate.at<float>(i,0)==0&&numCandidates==1)
			{
				draw_point.at<float>(after_mer,0)=rectspoint.at<float>(i,0);
				draw_point.at<float>(after_mer,1)=rectspoint.at<float>(i,1);
				draw_point.at<float>(after_mer,2)=rectspoint.at<float>(i,2);
				draw_point.at<float>(after_mer,3)=rectspoint.at<float>(i,3);
				draw_point.at<float>(after_mer,4)=rectspoint.at<float>(i,4);
				draw_point.at<float>(after_mer,5)=rectspoint.at<float>(i,5);
				predicate.at<float>(i,0)=-1;//������ֵ�õ���Ϊ-1
				after_mer++;

			}

		}

		if (num>0)
		{
		#pragma omp critical
			{
				draw_point.at<float>(after_mer,0)=rectspoint1;//+0.2*abs(rectspoint.at<float>(after_mer,0)-rectspoint.at<float>(after_mer,2));
				draw_point.at<float>(after_mer,1)=rectspoint2;//+0.2*abs(rectspoint.at<float>(after_mer,0)-rectspoint.at<float>(after_mer,2));
				draw_point.at<float>(after_mer,2)=rectspoint3;//-0.2*abs(rectspoint.at<float>(after_mer,0)-rectspoint.at<float>(after_mer,2));
				draw_point.at<float>(after_mer,3)=rectspoint4;//-0.2*abs(rectspoint.at<float>(after_mer,0)-rectspoint.at<float>(after_mer,2));
				draw_point.at<float>(after_mer,4)=abs(rectspoint.at<float>(after_mer,0)-rectspoint.at<float>(after_mer,2));//����Ĵ�С
				draw_point.at<float>(after_mer,5)=rec_score+rectspoint.at<float>(after_mer,5);
			}
			after_mer++;
		}

	}
	cvReleaseImage(&gray);
	releaseVector(height);
	releaseVector(width);
	releaseVector(pPyramidImages);
	return draw_point;
}

int FaceDetector::LoadTrainingParams(char* filename) {
	
	// string filename = "npd_trained.xml";
	FileStorage fsRead("/sdcard/npd_trained.xml",FileStorage::READ);
	if (!fsRead.isOpened())	
	{		
		LOGE("fail to open npd_trained.xml");
		return -1;
	}

	LOGE("npd_trained.xml is opened");
	TrainedParams.objSize = (int)fsRead["Object_Size"];
	FileNode fn;
	// Stages
	fn = fsRead["Stages"];	
	//Mat numStagesTrees,stageThreshold;
	fn["Num_Of_StageTrees"]>> numStagesTrees;
	fn["Stage_Threshold"] >> stageThreshold;	

	TrainedParams.numStages = (int)fn["Num_Of_Stage"];
	TrainedParams.pNumStageTrees = (int*) numStagesTrees.data;
	TrainedParams.pStageThreshold = (double*) stageThreshold.data;

	// Tree
	fn = fsRead["Tree"];
	//Mat treeRoot,fit;
	fn["Fit"]>>fit;
	fn["Tree_Root"]>>treeRoot;

	TrainedParams.numLeafNodes = fn["Num_Of_LeafNodes"];
	TrainedParams.pTreeRoot = (short*)treeRoot.data;
	TrainedParams.pFit = (double*)fit.data;

	// Branches
	fn = fsRead["Branches"];
	//Mat pix1,pix2,cutPts,lftChild,rgtChild;
	fn["Pixels1"]>>pix1;
	fn["Pixels2"]>>pix2;	
	fn["Cut_Points"]>>cutPts;

	fn["LeftChild"]>>lftChild;
	fn["RightChild"]>>rgtChild;
	TrainedParams.pPoints1 = (short*)pix1.data;
	TrainedParams.pPoints2 = (short*)pix2.data;
	TrainedParams.pCutpoint = (float*)cutPts.data;
	TrainedParams.pLeftChild = (short*)lftChild.data;
	TrainedParams.pRightChild = (short*)rgtChild.data;
	
	fsRead.release();

	return 1;
}
