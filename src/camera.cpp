#include "camera.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>

void Camera::pan(float x, float y) {
	viewDirty = true;

	glm::vec3 right = glm::transpose(glm::mat3(getRot())) * glm::vec3(1, 0, 0);
	glm::vec3 up = glm::transpose(glm::mat3(getRot())) * glm::vec3(0, 1, 0);

	origin += sensitivity * (x * right - y * up);
}

void Camera::orbit(float x, float y) {
	rotDirty = true;
	viewDirty = true;

	theta = glm::clamp(theta + y * sensitivity, -PI / 2 + EPSILON, PI / 2 - EPSILON);
	phi = phi + x * sensitivity;
}

void Camera::zoom(float z) {
	viewDirty = true;

	radius -= z * .1f;
}

const glm::mat4 &Camera::getView() {
	if (!viewDirty) { return viewMatrix; }

	viewDirty = false;
	viewMatrix = glm::translate(glm::vec3(0, 0, -radius)) * getRot() * glm::translate(-origin);
	return viewMatrix;
}

const glm::mat4 &Camera::getRot() {
	if (!rotDirty) { return rotMatrix; }

	rotDirty = false;
	rotMatrix = glm::rotate(glm::mat4(1), -theta, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1), -phi, glm::vec3(0, 1, 0));
	return rotMatrix;
}

void Camera::toPushConstantsPathtracer(PushConstantsPathtracer &pc) {
	getView();
	glm::mat3 rot = glm::transpose(glm::mat3(getRot()));

	pc.camForward = -rot[2];
	pc.camRight = rot[0];
	pc.camUp = rot[1];
	pc.camPos = origin - radius * pc.camForward;
	pc.fov = glm::radians(fov);
}

void Camera::processEvent(SDL_Event e) {
	if (e.type == SDL_EVENT_MOUSE_MOTION) {
		if (leftDown) orbit(-e.motion.xrel, -e.motion.yrel);
		else if (rightDown) pan(-e.motion.xrel, -e.motion.yrel);
	}
	else if (e.type == SDL_EVENT_MOUSE_WHEEL) zoom(e.wheel.y);
	else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) leftDown = true;
	else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_LEFT) leftDown = false;
	else if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) rightDown = true;
	else if (e.type == SDL_EVENT_MOUSE_BUTTON_UP && e.button.button == SDL_BUTTON_RIGHT) rightDown = false;
}

bool Camera::wasDirty() {
	return rotDirty || viewDirty;
}

void Camera::setCamera(glm::vec3 origin, float theta, float phi) {
	if (this->origin == origin && this->theta == theta && this->phi == phi) return;
	viewDirty = true;
	rotDirty = true;

	this->origin = origin;
	this->theta = theta;
	this->phi = phi;
}

const glm::vec3 &Camera::getOrigin() {
	return origin;
}

const float &Camera::getTheta() {
	return theta;
}

const float &Camera::getPhi() {
	return phi;
}