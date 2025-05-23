#pragma once

#include <cuda_runtime.h>
#include <curand_kernel.h>
#include <filesystem>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>


// Define some const var
#define MAX_IMAGES 32
#define NEIGHBOUR_NUM 9
#define MAX_SEARCH_RADIUS 4096
#define DEBUG_POINT_X 753
#define DEBUG_POINT_Y 259
//#define DEBUG_COST_LINE
//#define DEBUG_NEIGHBOUR


struct Camera {
	float K[9];
	float R[9];
	float t[3];
	float c[3];
	int height;
	int width;
	float depth_min;
	float depth_max;
};

struct PointList {
	float3 coord;
	float3 color;
};

enum RunState {
	FIRST_INIT,
	REFINE_INIT,
	REFINE_ITER,
};

enum PixelState {
	WEAK,
	STRONG,
	UNKNOWN
};

struct PatchMatchParams {
	int max_iterations = 3;
	int num_images = 5;
	float sigma_spatial = 5.0f;
	float sigma_color = 3.0f;
	int top_k = 4;
	float depth_min = 0.0f;
	float depth_max = 1.0f;
	bool geom_consistency = false;
	int strong_radius = 5;
	int strong_increment = 2;
	int weak_radius = 5;
	int weak_increment = 5;
	bool use_APD = true;
	int weak_peak_radius = 2;
	int rotate_time = 4;
	float ransac_threshold = 0.005;
	float geom_factor = 0.2f;
	RunState state;
};

struct Problem {
	int index;
	int ref_image_id;
	std::vector<int> src_image_ids;
	std::filesystem::path dense_folder;
	std::filesystem::path result_folder;
	int scale_size = 1;
	PatchMatchParams params;
	bool show_medium_result = false;
	int iteration;
};


#define CUDA_SAFE_CALL(error) CudaSafeCall(error, __FILE__, __LINE__)
#define CUDA_CHECK_ERROR() CudaCheckError(__FILE__, __LINE__)
#define M_PI 3.14159265358979323846

void CudaSafeCall(const cudaError_t error, const std::string& file, const int line);

void CudaCheckError(const char* file, const int line);

bool ReadBinMat(const std::filesystem::path &mat_path, cv::Mat &mat);

bool WriteBinMat(const std::filesystem::path &mat_path, const cv::Mat &mat);

bool ReadCamera(const std::filesystem::path &cam_path, Camera &cam);

bool ShowDepthMap(const std::filesystem::path &depth_path, const cv::Mat& depth, float depth_min, float depth_max);

bool ShowNormalMap(const std::filesystem::path &normal_path, const cv::Mat &normal);

bool ShowWeakImage(const std::filesystem::path &weak_path, const cv::Mat &weak);

bool ExportPointCloud(const std::filesystem::path& point_cloud_path, std::vector<PointList>& pointcloud);

std::string ToFormatIndex(int index);

template <typename TYPE>
void RescaleMatToTargetSize(const cv::Mat &src, cv::Mat &dst, const cv::Size2i &target_size);

void RunFusion(const std::filesystem::path &dense_folder, const std::vector<Problem> &problems);

struct cudaTextureObjects {
	cudaTextureObject_t images[MAX_IMAGES];
};

struct DataPassHelper {
	int width;
	int height;
	int ref_index;
	cudaTextureObjects *texture_objects_cuda;
	cudaTextureObjects *texture_depths_cuda;
	Camera *cameras_cuda;
	float4 *plane_hypotheses_cuda;
	curandState *rand_states_cuda;
	unsigned int *selected_views_cuda;
	short2 *neighbours_cuda;
	int *neighbours_map_cuda;
	uchar *weak_info_cuda;
	float *costs_cuda;
	PatchMatchParams *params;
	int2 debug_point;
	bool show_ncc_info;
	float4* fit_plane_hypotheses_cuda;
	uchar* weak_reliable_cuda;
	uchar *view_weight_cuda;
	short2 *weak_nearest_strong;
#ifdef DEBUG_COST_LINE
	float *weak_ncc_cost_cuda;
#endif // DEBUG_COST_LINE

};

class APD {
public:
	APD(const Problem &problem);
	~APD();

	void InuputInitialization();
	void CudaSpaceInitialization();
	void SetDataPassHelperInCuda();
	void RunPatchMatch();
	float4 GetPlaneHypothesis(int r, int c);
	cv::Mat GetPixelStates();
	cv::Mat GetSelectedViews();
	int GetWidth();
	int GetHeight();
	float GetDepthMin();
	float GetDepthMax();
private:
	void GenerateWeakFromImage();

	int num_images;
	int width;
	int height;
	Problem problem;
	// =========================
	// image host and cuda
	std::vector<cv::Mat> images;
	cudaTextureObjects texture_objects_host;
	cudaArray *cuArray[MAX_IMAGES];
	cudaTextureObjects *texture_objects_cuda;
	// =========================
	// depth host and cuda
	std::vector<cv::Mat> depths;
	cudaTextureObjects texture_depths_host;
	cudaArray *cuDepthArray[MAX_IMAGES];
	cudaTextureObjects *texture_depths_cuda;
	// =========================
	// camera host and cuda
	std::vector<Camera> cameras;
	Camera *cameras_cuda;
	// =========================
	// weak info host and cuda
	int weak_count;
	cv::Mat weak_info_host;
	uchar *weak_info_cuda;
	uchar *weak_reliable_cuda;
	short2 *weak_nearest_strong;
	// =========================
	// neighbour host and cuda
	short2 *neighbours_cuda;
	cv::Mat neighbours_map_host;
	int *neigbours_map_cuda;
	// =========================
	// plane hypotheses host and cuda
	float4 *plane_hypotheses_host;
	float4 *plane_hypotheses_cuda;
	float4 *fit_plane_hypotheses_cuda;
	// =========================
	// cost cuda 
	float *costs_cuda;
	// =========================
	// other var
	// params
	PatchMatchParams params_host;
	PatchMatchParams *params_cuda;
	// random states
	curandState *rand_states_cuda;
	// vis info
	cv::Mat selected_views_host;
	unsigned int *selected_views_cuda;
	// for easy data pass
	DataPassHelper helper_host;
	DataPassHelper *helper_cuda;
	// save view weigth
	uchar *view_weight_cuda;
	//export for test
#ifdef DEBUG_COST_LINE
	float *weak_ncc_cost_cuda;
#endif // DEBUG_COST_LINE
};
