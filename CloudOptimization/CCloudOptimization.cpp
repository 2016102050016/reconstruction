#include "CCloudOptimization.h"
#include "my_ply_interface.h"
#define MAX_CAM_NUM 10

pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
pcl::PointCloud<pcl::Normal>::Ptr cloud_normal(new pcl::PointCloud<pcl::Normal>);
pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_in(new pcl::PointCloud<pcl::PointXYZ>);
vector<Eigen::Vector3f> CamCenter;
cv::Mat current_img;
int current_img_width, current_img_height;
Eigen::Matrix3f current_R;
Eigen::Vector3f current_T;
void texture_color(float x, float y, float z, uchar rgb[]);
class _declspec(dllexport) CCloudOptimization
{
public:
	void Init(int sor_meank, double sor_stdThres, int sor_meank1, double sor_stdThres1, double mls_radius, CManageData *ImageData, bool isdelete_);
	void InsertPoint(cv::Mat p);
	void filter(int idx);
	void run();
private:
	int m_sor_meank;
	double m_mls_radius;
	int m_sor_meank1;
	double m_sor_stdThres1;
	double m_sor_stdThres;
	bool isdelete;
	CManageData *m_ImageData;
};

inline void reverse(float &x) { x = -x;}

void CCloudOptimization::Init(int sor_meank, double sor_stdThres, int sor_meank1, double sor_stdThres1, double mls_radius, CManageData *ImageData, bool isdelete_)
{
	m_sor_meank = sor_meank;
	m_sor_stdThres = sor_stdThres;
	m_sor_meank1 = sor_meank1;
	m_sor_stdThres1 = sor_stdThres1;
	m_mls_radius = mls_radius;
	m_ImageData = ImageData;
	isdelete = isdelete_;
	CamCenter.resize(m_ImageData->m_CampairNum);
	for (int i=0; i<m_ImageData->m_CampairNum; i++)
		cv::cv2eigen(m_ImageData->cam[i][0].CamCenter, CamCenter[i]);
}


void CCloudOptimization::InsertPoint(cv::Mat p)
{
	cloud_in->push_back(pcl::PointXYZ(p.ptr<double>(0)[0], p.ptr<double>(1)[0], p.ptr<double>(2)[0]));
}

void CCloudOptimization::filter(int idx)
{
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered (new pcl::PointCloud<pcl::PointXYZ>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered2 (new pcl::PointCloud<pcl::PointXYZ>);
	
	pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
	sor.setInputCloud (cloud_in);
	sor.setMeanK (m_sor_meank);
	sor.setStddevMulThresh (m_sor_stdThres);//face 0.3
	sor.filter (*cloud_filtered);
	printf("Initial points: %d\n",cloud_in->points.size());

	std::cerr << "Cloud after filtering: " << std::endl;
	std::cerr << *cloud_filtered;

	*cloud += *cloud_filtered;

	pcl::NormalEstimation<pcl::PointXYZ, pcl::Normal> ne;
	ne.setInputCloud (cloud_filtered);
	pcl::search::KdTree<pcl::PointXYZ>::Ptr tree (new pcl::search::KdTree<pcl::PointXYZ> ());
	ne.setSearchMethod (tree);
	pcl::PointCloud<pcl::Normal>::Ptr current_cloud_normals (new pcl::PointCloud<pcl::Normal>);
	ne.setRadiusSearch (2.5);			//to be changed
	ne.compute (*current_cloud_normals);
	ne.setViewPoint(CamCenter[idx][0], CamCenter[idx][1], CamCenter[idx][2]);
	*cloud_normal += *current_cloud_normals;
// 	char name[100];
// 	sprintf(name, "cloud_filter%d.ply", idx);
// 	pcl::io::savePLYFileBinary(name, *cloud_filtered);
	cloud_in->clear();
}

void CCloudOptimization::run()
{
// 	char filename[100];
// 	for (int i=0; i<5; i++)
// 	{
// 		sprintf(filename, "cloud%d.ply", i);
// 		pcl::io::loadPLYFile(filename, cloud_in);
// 		filter();
// 	}
	
	Eigen::Vector4f centroid;
	pcl::compute3DCentroid (*cloud,centroid);
	Eigen::Vector3f m_ObjectCenter = Eigen::Vector3f(centroid[0]-50, centroid[1], centroid[2]);
	
 	vector<vector<Eigen::Matrix3f>> R(m_ImageData->m_CampairNum);
	vector<vector<Eigen::Vector3f>> T(m_ImageData->m_CampairNum);
	for (int j=0; j<m_ImageData->m_CampairNum; j++)
	{
		R[j].resize(2);
		T[j].resize(2);
		cv::cv2eigen(m_ImageData->cam[j][0].P.colRange(0,3), R[j][0]);
		cv::cv2eigen(m_ImageData->cam[j][1].P.colRange(0,3), R[j][1]);
		cv::cv2eigen(m_ImageData->cam[j][0].P.col(3), T[j][0]);
		cv::cv2eigen(m_ImageData->cam[j][1].P.col(3), T[j][1]);
		m_ImageData->cam[j][0].bucket = new vector<int> *[m_ImageData->cam[j][0].bound.height];
		for (int i=0; i<m_ImageData->cam[j][0].bound.height; i++)
			m_ImageData->cam[j][0].bucket[i] = new vector<int> [m_ImageData->cam[j][0].bound.width];
	}

	//�������е㣬�ж������ӽǣ�Ȼ����ο����ͶӰ
	vector<vector<int>> cam_indices(m_ImageData->m_CampairNum);
	for (int i=0; i<cloud->size(); i++)
	{
		//�ҵ������ӽǣ�����һ�����
		Eigen::Vector3f current_point = cloud->points[i].getVector3fMap();
		Eigen::Vector3f current_direction_normed = m_ObjectCenter-current_point;
		current_direction_normed /= current_direction_normed.norm();
		float min_value = 2;
		int cam_belongedto = 0;
		for (int j=0; j<m_ImageData->m_CampairNum; j++)
		{
			Eigen::Vector3f current_camdir = CamCenter[j]-current_point;
			float current_value = current_direction_normed.dot(current_camdir)/current_camdir.norm();
			if (min_value > current_value)
			{
				min_value = current_value;
				cam_belongedto = j;
			}
		}
		cam_indices[cam_belongedto].push_back(i);
		//ͶӰ
		Boundary &current_bound = m_ImageData->cam[cam_belongedto][0].bound;
		Eigen::Vector3f current_imgPt = R[cam_belongedto][0] * current_point + T[cam_belongedto][0];
		int current_x = ROUND(current_imgPt[0]/current_imgPt[2])-current_bound.XL;
		int current_y = ROUND(current_imgPt[1]/current_imgPt[2])-current_bound.YL;
		if (current_x < 0 || current_x >= current_bound.width || \
			current_y < 0 || current_y >= current_bound.height)
			continue;
		m_ImageData->cam[cam_belongedto][0].bucket[current_y][current_x].push_back(i);
 	}

// 	for (int i=0; i<m_ImageData->m_CampairNum; i++)
// 	{
// 		boost::shared_ptr<std::vector<int> > indicesptr_temp (new std::vector<int> (cam_indices[i]));
// 		pcl::ExtractIndices<pcl::PointXYZ> extract;
// 		extract.setInputCloud (cloud);
// 		extract.setIndices (indicesptr_temp);
// 		extract.setNegative (false);
// 		extract.filter (*cloud_in);
// 		char name[100];
// 		sprintf(name, "cloud_filter%d.ply", i+10);
// 		pcl::io::savePLYFileBinary(name, *cloud_in);
// 	}

	boost::shared_ptr<std::vector<int> > indicesptr (new vector<int> );
	if (isdelete)
	{		
		//��ÿ���ӽ�
		vector<pcl::Normal,Eigen::aligned_allocator<pcl::Normal>> &normal = cloud_normal->points;
		vector<pcl::PointXYZ,Eigen::aligned_allocator<pcl::PointXYZ>> &point = cloud->points;
		int MatchBlockRadius = 2;
		int window_size = 2*MatchBlockRadius+1;
		int vec_size = square_(window_size)*3;

		for (int i=0; i<m_ImageData->m_CampairNum; i++)
		{
			vector<camera> &current_cam = m_ImageData->cam[i];
			int XL = current_cam[0].bound.XL;
			int XR = current_cam[0].bound.XR;
			int YL = current_cam[0].bound.YL;
			int YR = current_cam[0].bound.YR;
			int count0 = 0;
			for (int y=YL; y<=YR; y++)
			{
				uchar *mask_ptr = current_cam[0].mask.ptr<uchar>(y);
				for (int x=XL; x<=XR; x++)
				{
					if (mask_ptr[x]!=255)
						continue;
					int count = 0;
					vector<int> &current_bucket = current_cam[0].bucket[y-YL][x-XL];
					size_t candidate_size = current_bucket.size();
					switch(candidate_size)
					{
					case 0:
						break;
					case 1:
						count++;
						indicesptr->push_back(current_bucket[0]);
						break;
					case 2:
						//������㷨���෴���Ͷ��ӽ�ȥ
						if (normal[current_bucket[0]].getNormalVector3fMap().dot(		\
							normal[current_bucket[1]].getNormalVector3fMap()) < 0)
						{
							indicesptr->push_back(current_bucket[0]);
							indicesptr->push_back(current_bucket[1]);
							count+=2;
						}
						else
						{
							arma::vec vecL(vec_size), vecR(vec_size);
							double normL = m_ImageData->WindowToVec(current_cam[0].image, x-MatchBlockRadius, y-MatchBlockRadius, window_size, vecL);
							short temp_i = -1;
							double CurrentMaxValue = -1;
							for (int k=0; k<2; k++)
							{
								Eigen::Vector3f current_imgPt = R[i][1] * point[current_bucket[k]].getVector3fMap() + T[i][1];
								int current_x = ROUND(current_imgPt[0]/current_imgPt[2]);
								int current_y = ROUND(current_imgPt[1]/current_imgPt[2]);
								if ( current_cam[1].mask.at<uchar>(current_y, current_x) != 255)
								{
									count0++;
									continue;
								}
								double normR = m_ImageData->WindowToVec(current_cam[1].image, x-MatchBlockRadius, y-MatchBlockRadius, window_size, vecR);
								double CurrentValue = arma::dot(vecL, vecR)/(normR*normL);
								if (CurrentValue > CurrentMaxValue)
								{
									temp_i = k;
									CurrentMaxValue = CurrentValue;
								}
							}
							if (temp_i >= 0)
							{
								count++;
								indicesptr->push_back(current_bucket[temp_i]);
							}
						}
						break;
					default:
						//����2��ʱ�Ȱ�����Ӵ�С����
						vector<int> current_bucket_sorted_idx(candidate_size);
						vector<float> current_dist(candidate_size);
						//�����Ƿ��泯�����1��ʾ�ǣ�0��ʾ����
						vector<bool> current_direct(candidate_size);
						//�������ͳ���
						for (int l=0; l<candidate_size; l++)
						{
							Eigen::Vector3f direct = point[current_bucket[l]].getVector3fMap() - CamCenter[i];
							current_dist[l] = direct.norm();
							current_direct[l] = normal[current_bucket[l]].getNormalVector3fMap().dot(direct) < 0;
						}
						for (int l=0; l<candidate_size; l++)
						{
							float Max_dist = 0;
							int max_idx = -1;
							for (int k=0; k<candidate_size; k++)
							{
								if (Max_dist < current_dist[k])
								{
									Max_dist = current_dist[k];
									max_idx = k;
								}
							}
							current_dist[max_idx] = 0;
							current_bucket_sorted_idx[l] = max_idx;
						}
						current_dist.clear();
						int last_idx = 0;
						for (int l=1; l<candidate_size; l++)
						{
							if (current_direct[current_bucket_sorted_idx[last_idx]] == 
								current_direct[current_bucket_sorted_idx[l]] && l!=candidate_size-1)
								continue;

							short temp_i = last_idx;
							if (last_idx + 1 < l)
							{
								//����1���㷽����ͬ������Щ����ֻ����һ��
								arma::vec vecL(vec_size), vecR(vec_size);
								double normL = m_ImageData->WindowToVec(current_cam[0].image, x-MatchBlockRadius, y-MatchBlockRadius, window_size, vecL);
								double CurrentMaxValue = -1;
								for (int k=last_idx; k<l; k++)
								{
									Eigen::Vector3f current_imgPt = R[i][1] * point[current_bucket[current_bucket_sorted_idx[k]]].getVector3fMap() + T[i][1];
									int current_x = ROUND(current_imgPt[0]/current_imgPt[2]);
									int current_y = ROUND(current_imgPt[1]/current_imgPt[2]);
									if ( current_cam[1].mask.at<uchar>(current_y, current_x) != 255)
									{
										count0++;
										continue;
									}
									double normR = m_ImageData->WindowToVec(current_cam[1].image, x-MatchBlockRadius, y-MatchBlockRadius, window_size, vecR);
									double CurrentValue = arma::dot(vecL, vecR)/(normR*normL);
									if (CurrentValue > CurrentMaxValue)
									{
										temp_i = k;
										CurrentMaxValue = CurrentValue;
									}
								}
							}
							indicesptr->push_back(current_bucket[current_bucket_sorted_idx[temp_i]]);
							count++;
							last_idx = l;
						}
					}
				}
			}
		}

		for (int j=0; j<m_ImageData->m_CampairNum; j++)
		{
			for (int i=0; i<m_ImageData->cam[j][0].bound.height; i++)
				delete [] m_ImageData->cam[j][0].bucket[i];
			delete [] m_ImageData->cam[j][0].bucket;
		}
	}
	if (isdelete)
		printf("delete over\n");
	pcl::MovingLeastSquaresOMP<pcl::PointXYZ, pcl::PointNormal> mls;
	mls.setNumberOfThreads(8);
	mls.setComputeNormals (true);
	mls.setInputCloud (cloud);
	if (isdelete)
		mls.setIndices(indicesptr);

	mls.setSearchRadius (m_mls_radius);
	mls.setPolynomialFit (true);
	mls.setPolynomialOrder (1);
//	mls.setUpsamplingMethod (pcl::MovingLeastSquares<pcl::PointXYZ, pcl::PointNormal>::SAMPLE_LOCAL_PLANE );
//	mls.setUpsamplingRadius (1);
//	mls.setUpsamplingStepSize (0.8);
	pcl::PointCloud<pcl::PointNormal>::Ptr cloud_ms_normals (new pcl::PointCloud<pcl::PointNormal> ());
	mls.process (*cloud_ms_normals);
	cloud->clear();
	std::cerr << *cloud_ms_normals;
// 	pcl::StatisticalOutlierRemoval<pcl::PointNormal> sor;
// 	pcl::PointCloud<pcl::PointNormal>::Ptr cloud_final (new pcl::PointCloud<pcl::PointNormal> ());
// 	sor.setInputCloud (cloud_ms_normals);
// 	sor.setMeanK (m_sor_meank1);
// 	sor.setStddevMulThresh (m_sor_stdThres1);//face 0.3
// 	sor.filter (*cloud_final);
// 	printf("final points: %d\n",cloud_final->points.size());
	for (int i=0; i<m_ImageData->m_CampairNum; i++)
	{
		Eigen::Vector3f temp;
		cv::cv2eigen(m_ImageData->cam[i][1].CamCenter, temp);
		CamCenter.push_back(temp);
	}
	vector<boost::shared_ptr<vector<int> > > indicesptr_part(m_ImageData->m_CameraNum);
	for (int i=0; i<m_ImageData->m_CameraNum; i++)
	{
		indicesptr_part[i].reset(new vector<int>);
	}
//#pragma omp parallel for
	for (int i=0; i<cloud_ms_normals->size(); i++)
	{
		Eigen::Vector3f current_point = cloud_ms_normals->points[i].getVector3fMap();
		Eigen::Vector3f current_direction_normed = m_ObjectCenter-current_point;
		current_direction_normed /= current_direction_normed.norm();
		float min_three[3] = {2,2,2};//ascending order
		int idx[3] = {0,0,0};
		Eigen::Vector3f best_camdir;
		for (int j=0; j<m_ImageData->m_CameraNum; j++)
		{
			Eigen::Vector3f current_camdir = CamCenter[j]-current_point;
			float current_value = current_direction_normed.dot(current_camdir)/current_camdir.norm();
			if (current_value < min_three[2])
			{
				if (current_value > min_three[1])
				{
					min_three[2] = current_value;
					idx[2] = j;
				}else{
					if (current_value > min_three[0])
					{
						min_three[2] = min_three[1];
						idx[2] = idx[1];
						min_three[1] = current_value;
						idx[1] = j;
					}else{
						min_three[2] = min_three[1];
						idx[2] = idx[1];
						min_three[1] = min_three[0];
						idx[1] = idx[0];
						min_three[0] = current_value;
						idx[0] = j;
						best_camdir = current_camdir;
					}
				}
			}
		}
		indicesptr_part[idx[0]]->push_back(i);
		indicesptr_part[idx[1]]->push_back(i);
		indicesptr_part[idx[2]]->push_back(i);
		if (cloud_ms_normals->points[i].getNormalVector3fMap().dot(best_camdir) < 0)
		{
			reverse(cloud_ms_normals->points[i].normal_x);
			reverse(cloud_ms_normals->points[i].normal_y);
			reverse(cloud_ms_normals->points[i].normal_z);
		}	
	}

	

// 	for (int i=0; i<m_ImageData->m_CameraNum; i++)
// 	{
// 		pcl::ExtractIndices<pcl::PointNormal> extract;
// 		printf("%d: %d\n", i, indicesptr_part[i]->size());
// 		extract.setInputCloud (cloud_ms_normals);
// 		extract.setIndices (indicesptr_part[i]);
// 		extract.setNegative (false);
// 		pcl::PointCloud<pcl::PointNormal>::Ptr temp (new pcl::PointCloud<pcl::PointNormal> ());
// 		extract.filter (*temp);
// 		char name[MAX_PATH];
// 		sprintf(name, "cloud_filter%d.ply", i+10);
// 		pcl::io::savePLYFileBinary(name, *temp);
// 	}

	char cmd[MAX_PATH];
	sprintf(cmd, "mkdir tmp");
	system(cmd);
	pcl::io::savePLYFileBinary("tmp\\bigcloud.ply", *cloud_ms_normals);

// 	pcl::Poisson<pcl::PointNormal> poisson;
// 	poisson.setDepth(9);
// 	poisson.setInputCloud(cloud_final);
// 	poisson.setSolverDivide(9);
// 	pcl::PolygonMesh mesh;
// 	poisson.reconstruct(mesh);
// 	pcl::io::savePLYFileBinary("mesh.ply", mesh);
	//�ָ����ɲ�ɫ����
	pcl::PointCloud<pcl::RGB>::Ptr  cloud_color(new pcl::PointCloud<pcl::RGB>);
	pcl::PointCloud<pcl::PointXYZ>::Ptr  cloud_xyz(new pcl::PointCloud<pcl::PointXYZ>);
	current_img_width = m_ImageData->cam[0][0].image.cols;
	current_img_height = m_ImageData->cam[0][0].image.rows;
	for (int i=0; i<m_ImageData->m_CameraNum; i++)
	{
		pcl::ExtractIndices<pcl::PointNormal> extract;
		extract.setInputCloud (cloud_ms_normals);
		extract.setIndices (indicesptr_part[i]);
		extract.setNegative (false);
		pcl::PointCloud<pcl::PointNormal>::Ptr temp (new pcl::PointCloud<pcl::PointNormal> ());
		extract.filter (*temp);
		pcl::io::savePLYFileBinary("tmp\\cloud.ply", *temp);
		system("mesh.bat");

		int id2=i>4;
		int id1=i-id2*5;
		current_img = m_ImageData->cam[id1][id2].image;
		current_R = R[id1][id2];
		current_T = T[id1][id2];
		MyPlyIo myPlyIo(texture_color, 'b');
		char name[MAX_PATH];
		sprintf(name, "tmp\\color_%d.ply", i);
		if (myPlyIo.ReadAndWrite("tmp\\mesh_trimmer.ply", name))
			std::cout << "fail\n";

	}
	//meshlab script
	system("meshlab.bat");

	//texture mapping
	sprintf(cmd, "TextureStitcher.exe --in tmp\\bigmesh.ply --scans scans.txt --out %s --useKD", m_ImageData->outfilename.c_str());
	system(cmd);
}

void texture_color(float x, float y, float z, uchar rgb[])
{
	Eigen::Vector3f current_point;
	current_point[0] = x;
	current_point[1] = y;
	current_point[2] = z;
	Eigen::Vector3f current_imgPt = current_R * current_point + current_T;
	int current_x = ROUND(current_imgPt[0]/current_imgPt[2]);
	int current_y = ROUND(current_imgPt[1]/current_imgPt[2]);
	if (current_x < 0 || current_x >= current_img_width || \
		current_y < 0 || current_y >= current_img_height)
	{
		rgb[0] = 127;
		rgb[1] = 127;
		rgb[2] = 127;
		return ;
	}
	current_x *= 3;
	rgb[2] = current_img.ptr<uchar>(current_y)[current_x];
	rgb[1] = current_img.ptr<uchar>(current_y)[current_x+1];
	rgb[0] = current_img.ptr<uchar>(current_y)[current_x+2];
}