import cv2
import os

def split_video_to_frames(video_path, output_dir):
    # 1. Tạo thư mục output nếu chưa tồn tại
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)
        print(f"Đã tạo thư mục: {output_dir}")

    # 2. Mở file video
    cap = cv2.VideoCapture(video_path)
    
    if not cap.isOpened():
        print("Lỗi: Không thể mở file video. Kiểm tra lại đường dẫn.")
        return

    count = 0
    print("Đang xử lý, vui lòng đợi...")

    while True:
        # Đọc từng khung hình (frame)
        ret, frame = cap.read()

        # Nếu không còn frame nào hoặc lỗi thì dừng
        if not ret:
            break

        # 3. Lưu frame thành file .png
        # Định dạng frame_{số thứ tự}.png (ví dụ: frame_0001.png)
        filename = os.path.join(output_dir, f"frame_{count:04d}.png")
        cv2.imwrite(filename, frame)

        count += 1

    # Giải phóng bộ nhớ
    cap.release()
    print(f"Xử lý thành công! Đã tách được {count} ảnh vào thư mục '{output_dir}'.")

# Chạy chương trình
if __name__ == "__main__":
    split_video_to_frames('output.mkv', 'output')
