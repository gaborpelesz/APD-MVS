#include "APD.h"
#include <fstream>


/**
 * @brief Generate sample list from cluster list
 * 
 * @param cluster_list_path: pair.txt file with the following format:
 * | <n_images>
 * | <ref_image_id_1>
 * | <m_source_images> <src_image_id_1> <score_1> ... <src_image_id_m> <score_m>
 * | ...
 * | <ref_image_id_n>
 * | ...
 */
void GenerateSampleList(const std::filesystem::path &cluster_list_path, std::vector<Problem> &problems)
{
	problems.clear();

	std::ifstream file(cluster_list_path);
	std::stringstream iss;
	std::string line;

	int num_images;
	iss.clear();
	std::getline(file, line);
	iss.str(line);
	iss >> num_images;

	for (int i = 0; i < num_images; ++i) {
		Problem problem;
		problem.index = i;
		problem.src_image_ids.clear();
		iss.clear();
		std::getline(file, line);
		iss.str(line);
		iss >> problem.ref_image_id;

		problem.result_folder = cluster_list_path.parent_path() / "APD" / ToFormatIndex(problem.ref_image_id);
		std::filesystem::create_directories(problem.result_folder);

		int num_src_images;
		iss.clear();
		std::getline(file, line);
		iss.str(line);
		iss >> num_src_images;
		for (int j = 0; j < num_src_images; ++j) {
			int id;
			float score;
			iss >> id >> score;
			if (score <= 0.0f) {
				continue;
			}
			problem.src_image_ids.push_back(id);
		}
		problems.push_back(problem);
	}
}

int ComputeRoundNum(const std::vector<Problem> &problems) {
	assert(problems.size() > 0);
	std::filesystem::path image_path = problems[0].result_folder / "images" / (ToFormatIndex(problems[0].ref_image_id) + ".jpg");
	cv::Mat image = cv::imread(image_path.string());
	if (image.empty()) {
		return 0;
	}
	int max_size = std::max(image.cols, image.rows);
	int round_num = 1;
	while (max_size > 1000) {
		max_size /= 2;
		round_num++;
	}
	return round_num;
}


void ProcessProblem(const Problem &problem) {
	std::cout << "Processing image: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << "..." << std::endl;
	std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();

	APD APD(problem);
	APD.InuputInitialization();
	APD.CudaSpaceInitialization();
	APD.SetDataPassHelperInCuda();
	APD.RunPatchMatch();

	int width = APD.GetWidth(), height = APD.GetHeight();
	cv::Mat depth = cv::Mat(height, width, CV_32FC1);
	cv::Mat normal = cv::Mat(height, width, CV_32FC3);
	cv::Mat pixel_states = APD.GetPixelStates();
	for (int r = 0; r < height; ++r) {
		for (int c = 0; c < width; ++c) {
			float4 plane_hypothesis = APD.GetPlaneHypothesis(r, c);
			depth.at<float>(r, c) = plane_hypothesis.w;
			if (depth.at<float>(r, c) < APD.GetDepthMin() || depth.at<float>(r, c) > APD.GetDepthMax()) {
				depth.at<float>(r, c) = 0;
				pixel_states.at<uchar>(r, c) = UNKNOWN;
			}
			normal.at<cv::Vec3f>(r, c) = cv::Vec3f(plane_hypothesis.x, plane_hypothesis.y, plane_hypothesis.z);
		}
	}
	
	std::filesystem::path depth_path = problem.result_folder / "depths.dmb";
	WriteBinMat(depth_path, depth);
	std::filesystem::path normal_path = problem.result_folder / "normals.dmb";
	WriteBinMat(normal_path, normal);
	std::filesystem::path weak_path = problem.result_folder / "weak.bin";
	WriteBinMat(weak_path, pixel_states);
	std::filesystem::path selected_view_path = problem.result_folder / "selected_views.bin";
	WriteBinMat(selected_view_path, APD.GetSelectedViews());


	if (problem.show_medium_result) {
		std::filesystem::path depth_img_path = problem.result_folder / ("depth_" + std::to_string(problem.iteration) + ".jpg");
		std::filesystem::path normal_img_path = problem.result_folder / ("normal_" + std::to_string(problem.iteration) + ".jpg");
		std::filesystem::path weak_img_path = problem.result_folder / ("weak_" + std::to_string(problem.iteration) + ".jpg");
		ShowDepthMap(depth_img_path, depth, APD.GetDepthMin(), APD.GetDepthMax());
		ShowNormalMap(normal_img_path, normal);
		ShowWeakImage(weak_img_path, pixel_states);
	}
	std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
	std::cout << "Processing image: " << std::setw(8) << std::setfill('0') << problem.ref_image_id << " done!" << std::endl;
	std::cout << "Cost time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " ms" << std::endl;
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cerr << "USAGE: APD dense_folder [gpu_device_index]\n";
		return EXIT_FAILURE;
	}
	std::filesystem::path dense_folder(argv[1]);
	if (!std::filesystem::exists(dense_folder)) {
		std::cerr << "ERROR dense_folder: " << dense_folder << " not found\n";
		return EXIT_FAILURE;
	}
	std::filesystem::path output_folder = dense_folder / "APD";
	std::filesystem::create_directory(output_folder);
	// set cuda device for multi-gpu machine
	int gpu_index = 0;
	if (argc == 3) {
		gpu_index = std::atoi(argv[2]);
	}
	cudaSetDevice(gpu_index);

	// generate problems
	std::vector<Problem> problems;
	std::filesystem::path cluster_list_path = dense_folder / std::filesystem::path("pair.txt");
	if (!std::filesystem::exists(cluster_list_path)) {
		std::cerr << "ERROR cluster_list_path: " << cluster_list_path << " not found\n";
		return EXIT_FAILURE;
	}
	GenerateSampleList(cluster_list_path, problems);
	if (problems.size() == 0) {
		std::cerr << "ERROR problems.size(): " << problems.size() << "\n";
		std::cerr << "Images may error, check it!\n";
		return EXIT_FAILURE;
	}
	int num_images = problems.size();
	std::cout << "There are " << num_images << " problems needed to be processed!" << std::endl;

	int round_num = ComputeRoundNum(problems);

	std::cout << "Round nums: " << round_num << std::endl;
	int iteration_index = 0;
	for (int i = 0; i < round_num; ++i) {
		for (auto &problem : problems) {
			{
				auto &params = problem.params;
				if (i == 0) {
					params.state = FIRST_INIT;
					params.use_APD = false;
				}
				else {
					params.state = REFINE_INIT;
					params.use_APD = true;
					params.ransac_threshold = 0.01 - i * 0.00125;
					params.rotate_time = MIN(static_cast<int>(std::pow(2, i)), 4);
				}
				params.geom_consistency = false;
				params.max_iterations = 3;
				params.weak_peak_radius = 6;
			}
			problem.iteration = iteration_index;
			problem.show_medium_result = true;
			problem.scale_size = static_cast<int>(std::pow(2, round_num - 1 - i)); // scale 
			ProcessProblem(problem);
		}
		iteration_index++;
		for (int j = 0; j < 3; ++j) {
			for (auto &problem : problems) {
				{
					auto &params = problem.params;
					params.state = REFINE_ITER;
					if (i == 0) {
						params.use_APD = false;
					}
					else {
						params.use_APD = true;
						params.ransac_threshold = 0.01 - i * 0.00125;
						params.rotate_time = MIN(static_cast<int>(std::pow(2, i)), 4);
					}
					params.geom_consistency = true;
					params.max_iterations = 3;
					params.weak_peak_radius = MAX(4 - 2 * j, 2);
				}
				problem.iteration = iteration_index;
				problem.show_medium_result = true;
				problem.scale_size = static_cast<int>(std::pow(2, round_num - 1 - i)); // scale 
				ProcessProblem(problem);
			}
			iteration_index++;
		}
		std::cout << "Round: " << i << " done\n";
	}

	RunFusion(dense_folder, problems);
	{// delete files
		for (size_t i = 0; i < problems.size(); ++i) {
			const auto &problem = problems[i];
			remove(problem.result_folder / "weak.bin");
			remove(problem.result_folder / "depths.dmb");
			remove(problem.result_folder / "normals.dmb");
			remove(problem.result_folder / "selected_views.bin");
			//remove(problem.result_folder / path("neighbour.bin")); 
			//remove(problem.result_folder / path("neighbour_map.bin"));
		}
	}
	std::cout << "All done\n";
	return EXIT_SUCCESS;
}