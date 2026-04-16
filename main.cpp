#include <iostream>
#include <vector>
#include <string>
#include <opencv2/opencv.hpp>
#include <onnxruntime_cxx_api.h>

const int INPUT_WIDTH = 640;
const int INPUT_HEIGHT = 640;
const float CONFIDENCE_THRESHOLD = 0.45f;
const float NMS_THRESHOLD = 0.45f;

// Coco class names
const std::vector<std::string> CLASS_NAMES = {
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck", "boat", "traffic light",
    "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat", "dog", "horse", "sheep", "cow",
    "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle",
    "wine glass", "cup", "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch", "potted plant", "bed",
    "dining table", "toilet", "tv", "laptop", "mouse", "remote", "keyboard", "cell phone", "microwave", "oven",
    "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors", "teddy bear", "hair drier", "toothbrush"
};

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <model_path> <input_video> <output_video>\n";
        return 1;
    }

    std::string model_path = argv[1];
    std::string input_video = argv[2];
    std::string output_video = argv[3];

    // 1. Initialize ONNX Runtime with CUDA execution provider
    Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "YOLO11");
    Ort::SessionOptions session_options;
    
    // Enable CUDA provider
    OrtCUDAProviderOptions cuda_options;
    cuda_options.device_id = 0;
    session_options.AppendExecutionProvider_CUDA(cuda_options);
    
    // Initialize session
    Ort::Session session(env, model_path.c_str(), session_options);

    Ort::AllocatorWithDefaultOptions allocator;
    std::string input_name = session.GetInputNameAllocated(0, allocator).get();
    std::string output_name = session.GetOutputNameAllocated(0, allocator).get();
    
    std::vector<const char*> input_names = {input_name.c_str()};
    std::vector<const char*> output_names = {output_name.c_str()};

    std::vector<int64_t> input_shape = {1, 3, INPUT_HEIGHT, INPUT_WIDTH};

    // 2. Open Video File
    cv::VideoCapture cap(input_video);
    if (!cap.isOpened()) {
        std::cerr << "Error opening video file: " << input_video << "\n";
        return 1;
    }

    int frame_width = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_WIDTH));
    int frame_height = static_cast<int>(cap.get(cv::CAP_PROP_FRAME_HEIGHT));
    double fps = cap.get(cv::CAP_PROP_FPS);

    cv::VideoWriter writer(output_video, cv::VideoWriter::fourcc('m', 'p', '4', 'v'), fps, cv::Size(frame_width, frame_height));

    cv::Mat frame;
    auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

    while (cap.read(frame)) {
        if (frame.empty()) break;

        // 3. Preprocess Frame
        cv::Mat resized_frame;
        cv::resize(frame, resized_frame, cv::Size(INPUT_WIDTH, INPUT_HEIGHT));
        
        cv::Mat rgb_frame;
        cv::cvtColor(resized_frame, rgb_frame, cv::COLOR_BGR2RGB);

        cv::Mat blob;
        rgb_frame.convertTo(blob, CV_32F, 1.0f / 255.0f);

        // Convert HWC to CHW
        std::vector<cv::Mat> chw;
        cv::split(blob, chw);
        
        std::vector<float> input_tensor_values;
        size_t image_size = INPUT_HEIGHT * INPUT_WIDTH;
        input_tensor_values.reserve(3 * image_size);
        for (int i = 0; i < 3; ++i) {
            input_tensor_values.insert(input_tensor_values.end(), (float*)chw[i].datastart, (float*)chw[i].dataend);
        }

        Ort::Value input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input_tensor_values.data(), input_tensor_values.size(), input_shape.data(), input_shape.size());

        // 4. Run inference
        auto output_tensors = session.Run(Ort::RunOptions{nullptr}, input_names.data(), &input_tensor, 1, output_names.data(), 1);
        
        float* output_data = output_tensors[0].GetTensorMutableData<float>();
        
        // Output shape is [1, 84, 8400] for YOLOv8/11
        int num_classes = 80;
        int num_boxes = 8400;

        std::vector<cv::Rect> boxes;
        std::vector<float> confidences;
        std::vector<int> class_ids;

        float x_scale = (float)frame_width / INPUT_WIDTH;
        float y_scale = (float)frame_height / INPUT_HEIGHT;

        // Iterate over the 8400 bounding boxes
        for (int i = 0; i < num_boxes; ++i) {
            float max_conf = 0.0f;
            int best_class_id = -1;
            
            // Layout is [1, 84, 8400], so to access class c for box i: index = c * num_boxes + i
            // First 4 rows are cx, cy, w, h
            for (int c = 0; c < num_classes; ++c) {
                float conf = output_data[(4 + c) * num_boxes + i];
                if (conf > max_conf) {
                    max_conf = conf;
                    best_class_id = c;
                }
            }

            if (max_conf > CONFIDENCE_THRESHOLD) {
                float cx = output_data[0 * num_boxes + i];
                float cy = output_data[1 * num_boxes + i];
                float w = output_data[2 * num_boxes + i];
                float h = output_data[3 * num_boxes + i];

                int left = static_cast<int>((cx - w / 2) * x_scale);
                int top  = static_cast<int>((cy - h / 2) * y_scale);
                int width = static_cast<int>(w * x_scale);
                int height = static_cast<int>(h * y_scale);

                boxes.push_back(cv::Rect(left, top, width, height));
                confidences.push_back(max_conf);
                class_ids.push_back(best_class_id);
            }
        }

        // 5. Non-Maximum Suppression (NMS)
        std::vector<int> indices;
        cv::dnn::NMSBoxes(boxes, confidences, CONFIDENCE_THRESHOLD, NMS_THRESHOLD, indices);

        // 6. Draw annotations
        for (int idx : indices) {
            cv::Rect box = boxes[idx];
            int class_id = class_ids[idx];
            float conf = confidences[idx];

            cv::rectangle(frame, box, cv::Scalar(0, 255, 0), 2);
            std::string label = CLASS_NAMES[class_id] + " " + cv::format("%.2f", conf);
            
            int baseLine;
            cv::Size labelSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseLine);
            cv::rectangle(frame, cv::Point(box.x, box.y - labelSize.height),
                          cv::Point(box.x + labelSize.width, box.y + baseLine), cv::Scalar(0, 255, 0), cv::FILLED);
            cv::putText(frame, label, cv::Point(box.x, box.y), cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(0, 0, 0), 1);
        }

        writer.write(frame);
    }

    cap.release();
    writer.release();

    std::cout << "Done! Output saved to " << output_video << std::endl;
    return 0;
}
