#include "camera/EditorCamera.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace StellarAlia::Editor {

glm::quat EditorCamera::Rotation() const {
    return glm::angleAxis(glm::radians(yaw),   glm::vec3{0.f, 1.f, 0.f})
         * glm::angleAxis(glm::radians(pitch), glm::vec3{1.f, 0.f, 0.f});
}

void EditorCamera::Update(const InputSystem& input, float dt, bool mouseLook) {
    if (mouseLook) {
        const glm::vec2 look = input.ReadVec2("Look");
        yaw   -= look.x;
        pitch -= look.y;
        pitch  = glm::clamp(pitch, -80.f, 80.f);
    }

    const glm::quat  rot   = Rotation();
    const glm::vec3  fwd   = rot * glm::vec3{ 0.f,  0.f, -1.f };
    const glm::vec3  right = rot * glm::vec3{ 1.f,  0.f,  0.f };
    const glm::vec2  move  = input.ReadVec2("Move");
    const float      spd   = input.IsActive("Sprint") ? sprintSpeed : moveSpeed;

    position += (right * move.x + fwd * move.y) * spd * dt;
}

CameraData EditorCamera::GetCameraData(float aspectRatio) const {
    const glm::mat4 worldTransform =
        glm::translate(glm::mat4(1.f), position) *
        glm::mat4_cast(Rotation());

    CameraData out;
    out.view          = glm::inverse(worldTransform);
    out.proj          = glm::perspective(fovY, aspectRatio, nearPlane, farPlane);
    out.proj[1][1]   *= -1.f;   // Vulkan Y-flip
    out.worldPosition = position;
    return out;
}

} // namespace StellarAlia::Editor
