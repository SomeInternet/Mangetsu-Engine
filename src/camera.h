#pragma once
#include <SDL3/SDL.h>

#include "pushconstants.h"

constexpr float EPSILON = 0.000001f;
constexpr float PI = 3.1415926535897932384626433832795028841971f;

class Camera {
public:
	void pan(float x, float y);
	void orbit(float x, float y);
	void zoom(float z);

	void toPushConstantsPathtracer(PushConstantsPathtracer &pc);

	const glm::mat4 &getView();
	const glm::mat4 &getRot();
	
	void processEvent(SDL_Event e);

	bool wasDirty();

private:
	bool leftDown{ false };
	bool rightDown{ false };

	bool rotDirty{ true };
	bool viewDirty{ true };

	float radius{ 10.f };
	float velocity{ 1.f };
	glm::vec3 origin{ 0 };

	float sensitivity{ .01f };

	//Spherical polar coordinates
	float theta{ 0.f };
	float phi{ 0.f };	

	float fov{ 70.f }; //FOV in degrees

	glm::mat4 viewMatrix;
	glm::mat4 rotMatrix;
};