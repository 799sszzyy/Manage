#pragma once

#include <QJsonArray>
#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QSpinBox;

namespace manage::data {
struct Material;
}

namespace manage::desktop {

class ApiClient;
struct ApiResponse;

class ExcelToolsWidget final : public QWidget {
public:
    explicit ExcelToolsWidget(ApiClient* apiClient, QWidget* parent = nullptr);

private:
    void saveMaterialTemplate();
    void importMaterials();
    void submitMaterialImport(const QString& path, bool validateOnly);
    void exportMaterials();
    void fetchMaterialPage(
        int page,
        const QString& outputPath,
        std::vector<manage::data::Material> materials
    );
    void exportBom();
    void exportQuote();
    void updatePermissions();
    void setBusy(bool busy, const QString& message = {});
    void showApiError(const QString& action, const ApiResponse& response);

    ApiClient* apiClient_{};
    QPushButton* templateButton_{};
    QPushButton* importButton_{};
    QPushButton* materialsButton_{};
    QPushButton* bomButton_{};
    QPushButton* quoteButton_{};
    QSpinBox* bomIdSpin_{};
    QSpinBox* quoteIdSpin_{};
    QLabel* statusLabel_{};
    bool busy_{};
};

} // namespace manage::desktop
