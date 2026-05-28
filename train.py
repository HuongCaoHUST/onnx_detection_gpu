# ============================================
# 0. Cài đặt thư viện (nếu chưa có)
# ============================================
# !pip install timm pandas matplotlib tqdm opencv-python scikit-learn onnx onnxruntime

### Bản này chạy oke

import os
import pandas as pd
import numpy as np
import torch
import torch.nn as nn
import torch.optim as optim
from torch.utils.data import Dataset, DataLoader
from torchvision import transforms
import torchvision.transforms.functional as TF
from PIL import Image
from tqdm.notebook import tqdm
import matplotlib.pyplot as plt
from sklearn.metrics import (classification_report, accuracy_score, 
                             f1_score, precision_score, recall_score,
                             hamming_loss)
import timm

# ============================================
# 1. Cấu hình đường dẫn
# ============================================
TRAIN_DIR = '/kaggle/working/coal_miners_datasets-5/train'
VALID_DIR = '/kaggle/working/coal_miners_datasets-5/valid'
TEST_DIR  = '/kaggle/working/coal_miners_datasets-5/test'

TRAIN_CSV = os.path.join(TRAIN_DIR, '_classes.csv')
VALID_CSV = os.path.join(VALID_DIR, '_classes.csv')
TEST_CSV  = os.path.join(TEST_DIR, '_classes.csv')

CLASSES = ['Helmet', 'Lamp', 'Mask', 'Shoes', 'Suit']
NUM_CLASSES = len(CLASSES)
DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"🔥 Thiết bị: {DEVICE}")

# ============================================
# 2. Dataset với SquarePad giữ tỉ lệ ảnh
# ============================================
class SquarePad:
    def __call__(self, image):
        w, h = image.size
        max_wh = max(w, h)
        hp = int((max_wh - w) / 2)
        vp = int((max_wh - h) / 2)
        padding = (hp, vp, max_wh - w - hp, max_wh - h - vp)
        return TF.pad(image, padding, 0, 'constant')

class PPEDataset(Dataset):
    def __init__(self, dataframe, img_dir, transform=None):
        self.df = dataframe.reset_index(drop=True)
        self.img_dir = img_dir
        self.transform = transform

    def __len__(self):
        return len(self.df)

    def __getitem__(self, idx):
        img_name = os.path.join(self.img_dir, self.df.iloc[idx]['filename'])
        image = Image.open(img_name).convert('RGB')
        labels = self.df.iloc[idx][CLASSES].values.astype('float32')
        labels = torch.tensor(labels)

        if self.transform:
            image = self.transform(image)
        return image, labels

# ============================================
# 3. Augmentations
# ============================================
train_transforms = transforms.Compose([
    SquarePad(),
    transforms.Resize((224, 224)),
    transforms.RandomHorizontalFlip(p=0.5),
    transforms.RandomRotation(degrees=15),
    transforms.RandomAffine(degrees=0, translate=(0.1, 0.1), scale=(0.9, 1.1)),
    transforms.ColorJitter(brightness=0.3, contrast=0.3, saturation=0.2, hue=0.1),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
    transforms.RandomErasing(p=0.2, scale=(0.02, 0.1)),
])

val_test_transforms = transforms.Compose([
    SquarePad(),
    transforms.Resize((224, 224)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.485, 0.456, 0.406], std=[0.229, 0.224, 0.225]),
])

# ============================================
# 4. Load dữ liệu
# ============================================
df_train = pd.read_csv(TRAIN_CSV)
df_valid = pd.read_csv(VALID_CSV)
df_test  = pd.read_csv(TEST_CSV)

print(f"Train: {len(df_train)} | Valid: {len(df_valid)} | Test: {len(df_test)}")

train_dataset = PPEDataset(df_train, TRAIN_DIR, train_transforms)
valid_dataset = PPEDataset(df_valid, VALID_DIR, val_test_transforms)
test_dataset  = PPEDataset(df_test,  TEST_DIR,  val_test_transforms)

BATCH_SIZE = 64   # giảm nếu GPU ít RAM
train_loader = DataLoader(train_dataset, batch_size=BATCH_SIZE, shuffle=True, num_workers=2)
valid_loader = DataLoader(valid_dataset, batch_size=BATCH_SIZE, shuffle=False, num_workers=2)
test_loader  = DataLoader(test_dataset,  batch_size=16, shuffle=False, num_workers=2)

# ============================================
# 5. Mô hình EfficientNet-Lite0
# ============================================
def create_model():
    model = timm.create_model('tf_efficientnet_lite0.in1k', pretrained=True)
    in_features = model.classifier.in_features
    model.classifier = nn.Linear(in_features, NUM_CLASSES)
    return model

model = create_model().to(DEVICE)
print(f"📦 Số tham số: {sum(p.numel() for p in model.parameters()):,}")

# ============================================
# 6. Focal Loss cho multi-label
# ============================================
class FocalLoss(nn.Module):
    def __init__(self, alpha=0.25, gamma=2.0, reduction='mean'):
        super().__init__()
        self.alpha = alpha
        self.gamma = gamma
        self.reduction = reduction

    def forward(self, inputs, targets):
        bce_loss = nn.functional.binary_cross_entropy_with_logits(inputs, targets, reduction='none')
        pt = torch.exp(-bce_loss)
        focal = self.alpha * (1 - pt) ** self.gamma * bce_loss
        if self.reduction == 'mean':
            return focal.mean()
        elif self.reduction == 'sum':
            return focal.sum()
        return focal

criterion = FocalLoss(alpha=0.5, gamma=2.0)

# ============================================
# 7. Optimizer & Scheduler
# ============================================
optimizer = optim.AdamW(model.parameters(), lr=1e-3, weight_decay=1e-4)
total_steps = len(train_loader) * 50   # 50 epochs
scheduler = optim.lr_scheduler.CosineAnnealingLR(optimizer, T_max=total_steps, eta_min=1e-6)

# ============================================
# 8. Hàm tìm threshold tối ưu trên validation
# ============================================
def find_best_threshold(model, dataloader, device):
    model.eval()
    all_probs = []
    all_labels = []
    with torch.no_grad():
        for images, labels in tqdm(dataloader, desc="Tìm threshold"):
            images = images.to(device)
            outputs = model(images)
            probs = torch.sigmoid(outputs).cpu().numpy()
            all_probs.append(probs)
            all_labels.append(labels.numpy())
    all_probs = np.concatenate(all_probs)
    all_labels = np.concatenate(all_labels)
    
    best_thresh = 0.5
    best_f1 = 0.0
    for thresh in np.arange(0.3, 0.8, 0.02):
        preds_bin = (all_probs >= thresh).astype(int)
        f1 = f1_score(all_labels, preds_bin, average='macro')
        if f1 > best_f1:
            best_f1 = f1
            best_thresh = thresh
    return best_thresh, best_f1

# ============================================
# 9. Hàm đánh giá đầy đủ metrics
# ============================================
def evaluate_full_metrics(model, dataloader, device, threshold=0.5):
    model.eval()
    all_probs = []
    all_labels = []
    with torch.no_grad():
        for images, labels in tqdm(dataloader, desc="Đánh giá"):
            images = images.to(device)
            outputs = model(images)
            probs = torch.sigmoid(outputs).cpu().numpy()
            all_probs.append(probs)
            all_labels.append(labels.numpy())
    all_probs = np.concatenate(all_probs)
    all_labels = np.concatenate(all_labels)
    all_preds = (all_probs >= threshold).astype(int)
    
    # Exact match accuracy
    exact_acc = accuracy_score(all_labels, all_preds)
    
    # Per-class metrics
    per_class_precision = precision_score(all_labels, all_preds, average=None, zero_division=0)
    per_class_recall    = recall_score(all_labels, all_preds, average=None, zero_division=0)
    per_class_f1        = f1_score(all_labels, all_preds, average=None, zero_division=0)
    
    # Macro / Micro / Weighted
    macro_precision = precision_score(all_labels, all_preds, average='macro', zero_division=0)
    macro_recall    = recall_score(all_labels, all_preds, average='macro', zero_division=0)
    macro_f1        = f1_score(all_labels, all_preds, average='macro', zero_division=0)
    
    micro_precision = precision_score(all_labels, all_preds, average='micro', zero_division=0)
    micro_recall    = recall_score(all_labels, all_preds, average='micro', zero_division=0)
    micro_f1        = f1_score(all_labels, all_preds, average='micro', zero_division=0)
    
    weighted_precision = precision_score(all_labels, all_preds, average='weighted', zero_division=0)
    weighted_recall    = recall_score(all_labels, all_preds, average='weighted', zero_division=0)
    weighted_f1        = f1_score(all_labels, all_preds, average='weighted', zero_division=0)
    
    # Hamming loss
    ham_loss = hamming_loss(all_labels, all_preds)
    
    # Subset accuracy (giống exact_acc)
    subset_acc = exact_acc
    
    # Classification report (dạng text)
    class_report = classification_report(all_labels, all_preds, target_names=CLASSES, zero_division=0)
    
    # Lưu vào dict
    results = {
        'exact_match_accuracy': exact_acc,
        'subset_accuracy': subset_acc,
        'hamming_loss': ham_loss,
        'macro_precision': macro_precision, 'macro_recall': macro_recall, 'macro_f1': macro_f1,
        'micro_precision': micro_precision, 'micro_recall': micro_recall, 'micro_f1': micro_f1,
        'weighted_precision': weighted_precision, 'weighted_recall': weighted_recall, 'weighted_f1': weighted_f1,
        'per_class_precision': per_class_precision,
        'per_class_recall': per_class_recall,
        'per_class_f1': per_class_f1,
        'classification_report': class_report
    }
    return results

# ============================================
# 10. Huấn luyện với Early Stopping
# ============================================
EPOCHS = 50
PATIENCE = 20
best_val_loss = float('inf')
best_val_f1 = 0.0
best_threshold = 0.5
counter = 0
SAVE_PATH = 'ppe_efficientnet_lite0_best.pth'

for epoch in range(EPOCHS):
    # Train
    model.train()
    train_loss = 0.0
    loop = tqdm(train_loader, desc=f"Epoch {epoch+1}/{EPOCHS} [Train]")
    for images, labels in loop:
        images, labels = images.to(DEVICE), labels.to(DEVICE)
        optimizer.zero_grad()
        outputs = model(images)
        loss = criterion(outputs, labels)
        loss.backward()
        optimizer.step()
        scheduler.step()
        train_loss += loss.item()
        loop.set_postfix(loss=loss.item())
    avg_train_loss = train_loss / len(train_loader)
    
    # Validation
    model.eval()
    val_loss = 0.0
    with torch.no_grad():
        for images, labels in valid_loader:
            images, labels = images.to(DEVICE), labels.to(DEVICE)
            outputs = model(images)
            loss = criterion(outputs, labels)
            val_loss += loss.item()
    avg_val_loss = val_loss / len(valid_loader)
    
    # Tìm threshold tối ưu trên validation set
    best_thresh, val_f1 = find_best_threshold(model, valid_loader, DEVICE)
    
    print(f"Epoch {epoch+1}: Train Loss={avg_train_loss:.4f} | Val Loss={avg_val_loss:.4f} | Val Macro F1={val_f1:.4f} (best_thresh={best_thresh:.2f})")
    
    # Save best model dựa trên F1
    if val_f1 > best_val_f1:
        best_val_f1 = val_f1
        best_threshold = best_thresh
        torch.save(model.state_dict(), SAVE_PATH)
        counter = 0
        print(f"  💾 Saved best model (F1={best_val_f1:.4f})")
    else:
        counter += 1
        if counter >= PATIENCE:
            print(f"🛑 Early stopping at epoch {epoch+1}")
            break

print(f"\n✅ Training finished. Best Val Macro F1 = {best_val_f1:.4f} with threshold = {best_threshold:.2f}")

# ============================================
# 11. Đánh giá trên tập Test với threshold tối ưu
# ============================================
model.load_state_dict(torch.load(SAVE_PATH))
model.eval()

print(f"\n========== ĐÁNH GIÁ TRÊN TẬP TEST (threshold = {best_threshold:.2f}) ==========")
test_metrics = evaluate_full_metrics(model, test_loader, DEVICE, threshold=best_threshold)

# In kết quả
print(f"\n📌 Exact Match Accuracy (Subset Accuracy): {test_metrics['exact_match_accuracy']*100:.2f}%")
print(f"📌 Hamming Loss: {test_metrics['hamming_loss']:.4f} (càng nhỏ càng tốt)")
print(f"\n--- Macro Averages ---")
print(f"Precision: {test_metrics['macro_precision']:.4f} | Recall: {test_metrics['macro_recall']:.4f} | F1: {test_metrics['macro_f1']:.4f}")
print(f"\n--- Micro Averages ---")
print(f"Precision: {test_metrics['micro_precision']:.4f} | Recall: {test_metrics['micro_recall']:.4f} | F1: {test_metrics['micro_f1']:.4f}")
print(f"\n--- Weighted Averages ---")
print(f"Precision: {test_metrics['weighted_precision']:.4f} | Recall: {test_metrics['weighted_recall']:.4f} | F1: {test_metrics['weighted_f1']:.4f}")

print("\n--- Per-class metrics ---")
for i, cls in enumerate(CLASSES):
    print(f"{cls:8s} | Prec: {test_metrics['per_class_precision'][i]:.4f} | Recall: {test_metrics['per_class_recall'][i]:.4f} | F1: {test_metrics['per_class_f1'][i]:.4f}")

print("\n--- Detailed Classification Report ---")
print(test_metrics['classification_report'])

# ============================================
# 12. Trực quan hóa một vài ảnh dự đoán
# ============================================
def visualize_predictions(model, dataloader, threshold, num_samples=5):
    model.eval()
    images, labels = next(iter(dataloader))
    images = images.to(DEVICE)
    with torch.no_grad():
        outputs = model(images)
        probs = torch.sigmoid(outputs)
        preds = (probs > threshold).int().cpu()
    
    fig, axes = plt.subplots(1, num_samples, figsize=(20, 5))
    if num_samples == 1:
        axes = [axes]
    for i in range(num_samples):
        img = images[i].cpu().numpy().transpose(1,2,0)
        mean = np.array([0.485,0.456,0.406])
        std = np.array([0.229,0.224,0.225])
        img = std * img + mean
        img = np.clip(img,0,1)
        true_lbl = [CLASSES[j] for j in range(NUM_CLASSES) if labels[i][j]==1]
        pred_lbl = [CLASSES[j] for j in range(NUM_CLASSES) if preds[i][j]==1]
        color = "green" if set(true_lbl) == set(pred_lbl) else "red"
        axes[i].imshow(img)
        axes[i].set_title(f"True: {', '.join(true_lbl)}\nPred: {', '.join(pred_lbl)}", color=color, fontsize=10)
        axes[i].axis('off')
    plt.tight_layout()
    plt.show()

visualize_predictions(model, test_loader, threshold=best_threshold, num_samples=5)

# ============================================
# 13. Lượng tử hóa INT8 và xuất ONNX (cho edge device)
# ============================================
print("\n===== XUẤT MÔ HÌNH CHO EDGE DEVICE =====")

# Lượng tử hóa POST-TRAINING INT8
model.cpu()  # chuyển về CPU để lượng tử hóa
model.eval()
model.qconfig = torch.quantization.get_default_qconfig('fbgemm')
model_prepared = torch.quantization.prepare(model, inplace=False)

# Calibration với một số batch từ train loader
with torch.no_grad():
    for i, (images, _) in enumerate(train_loader):
        if i >= 10:  # dùng 10 batch (640 ảnh)
            break
        model_prepared(images)

model_quantized = torch.quantization.convert(model_prepared)
torch.save(model_quantized.state_dict(), 'ppe_efficientnet_lite0_quantized.pth')
print("✅ Đã lưu model lượng tử INT8 (CPU)")

# Xuất sang ONNX (float32)
dummy_input = torch.randn(1, 3, 224, 224)
torch.onnx.export(model, dummy_input, "ppe_efficientnet_lite0.onnx",
                  input_names=['input'], output_names=['output'],
                  dynamic_axes={'input': {0: 'batch_size'}, 'output': {0: 'batch_size'}},
                  opset_version=11)
print("✅ Đã xuất mô hình sang ONNX (float32)")

print("\n🎉 Hoàn tất toàn bộ pipeline!")