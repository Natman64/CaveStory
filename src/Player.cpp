#include "Player.h"
#include "Game.h"

#include <cmath>
#include <boost/math/special_functions/round.hpp>
#include <string>
#include "Map.h"
#include "Rectangle.h"

namespace
{
	// Walk Motion
	const units::Acceleration kWalkingAcceleration = 0.00083007812f;
	const units::Velocity kMaxSpeedX = 0.15859375f;
	const units::Acceleration kFriction = 0.00049804587f;

	// Fall Motion
	const units::Acceleration kGravity = 0.00078125f;
	const units::Velocity kMaxSpeedY = 0.2998046875f;

	// Jump Motion
	const units::Velocity kJumpSpeed = 0.25f;
	const units::Velocity kShortJumpSpeed = kJumpSpeed / 1.5f;
	const units::Acceleration kAirAcceleration = 0.0003125f;
	const units::Acceleration kJumpGravity = 0.0003125f;

	// Sprites
	const std::string kSpriteFilePath("content/MyChar.bmp");

	// Sprite Frames
	const units::Frame kCharacterFrame = 0;

	const units::Frame kWalkFrame = 0;
	const units::Frame kStandFrame = 0;
	const units::Frame kJumpFrame = 1;
	const units::Frame kFallFrame = 2;
	const units::Frame kUpFrameOffset = 3;
	const units::Frame kDownFrame = 6;
	const units::Frame kBackFrame = 7;

	// Walk Animation
	const units::Frame kNumWalkFrames = 3;
	const units::FPS kWalkFps = 15;

	// Collision Rectangles
	const Rectangle kCollisionX(
		6,
		10,
		20, 
		12);

	const Rectangle kCollisionY(
		10,
		2,
		12,
		30);

	const units::MS kInvincibleFlashTime = 50;
	const units::MS kInvincibleTime = 3000;

	struct CollisionInfo
	{
		bool collided;
		units::Tile row, col;
	};

	CollisionInfo getWallCollisionInfo(const Map& map, const Rectangle& rectangle)
	{
		CollisionInfo info = { false, 0, 0 };
		std::vector<Map::CollisionTile> tiles(map.getCollidingTiles(rectangle));
		for (size_t i = 0; i < tiles.size(); ++i)
		{
			if (tiles[i].tile_type == Map::WALL_TILE)
			{
				info.collided = true;
				info.row = tiles[i].row;
				info.col = tiles[i].col;
				break;
			}
		}

		return info;
	}
}

bool operator<(const Player::SpriteState& a, const Player::SpriteState& b)
{
	if (a.motion_type != b.motion_type)
	{
		return a.motion_type < b.motion_type;
	}
	if (a.horizontal_facing != b.horizontal_facing)
	{
		return a.horizontal_facing < b.horizontal_facing;
	}
	if (a.vertical_facing != b.vertical_facing)
	{
		return a.vertical_facing < b.vertical_facing;
	}
	return false;
}

Player::Player(Graphics& graphics, units::Game x, units::Game y) :
	x_(x),
	y_(y),
	velocity_x_(0.0f),
	velocity_y_(0.0f),
	acceleration_x_(0),
	horizontal_facing_(LEFT),
	vertical_facing_(HORIZONTAL),
	on_ground_(false),
	jump_active_(false),
	interacting_(false),
	health_(graphics),
	invincible_timer_(kInvincibleTime),
	polar_star_(graphics)
{
	initializeSprites(graphics);
}

void Player::initializeSprites(Graphics& graphics)
{
	for (int motion_type = FIRST_MOTION_TYPE; motion_type < LAST_MOTION_TYPE; ++motion_type)
	{
		for (int horizontal_facing = FIRST_HORIZONTAL_FACING; horizontal_facing < LAST_HORIZONTAL_FACING; ++horizontal_facing)
		{
			for (int vertical_facing = FIRST_VERTICAL_FACING; vertical_facing < LAST_VERTICAL_FACING; ++vertical_facing)
			{
				initializeSprite(graphics, SpriteState(
					(MotionType)motion_type, 
					(HorizontalFacing)horizontal_facing, 
					(VerticalFacing)vertical_facing));
			}
		}
	}
}

void Player::initializeSprite(Graphics& graphics, const SpriteState& sprite_state)
{
	units::Tile  tile_y = sprite_state.horizontal_facing == LEFT ?
		kCharacterFrame
		: (1 + kCharacterFrame);

	units::Tile tile_x = 0;

	switch (sprite_state.motion_type)
	{
	case WALKING:
		tile_x = kWalkFrame;
		break;

	case STANDING:
		tile_x = kStandFrame;
		break;
		
	case INTERACTING:
		tile_x = kBackFrame;
		break;

	case JUMPING:
		tile_x = kJumpFrame;
		break;

	case FALLING:
		tile_x = kFallFrame;
		break;

	case LAST_MOTION_TYPE:
		break;
	}

	tile_x = sprite_state.vertical_facing == UP ?
		tile_x + kUpFrameOffset :
		tile_x;

	if (sprite_state.motion_type == WALKING)
	{
		//create an animated sprite
		sprites_[sprite_state] = 
			boost::shared_ptr<Sprite>(new AnimatedSprite(
				graphics,
				kSpriteFilePath,
				units::tileToPixel(tile_x), units::tileToPixel(tile_y),
				units::tileToPixel(1), units::tileToPixel(1), kWalkFps, kNumWalkFrames)); 
	}
	else
	{
		if (sprite_state.vertical_facing == DOWN &&
			(sprite_state.motion_type == JUMPING || sprite_state.motion_type == FALLING))
		{
			tile_x = kDownFrame;
		}

		//create a static sprite
		sprites_[sprite_state] =
			boost::shared_ptr<Sprite>(new Sprite(
				graphics,
				kSpriteFilePath,
				units::tileToPixel(tile_x), units::tileToPixel(tile_y),
				units::tileToPixel(1), units::tileToPixel(1)));
	}

}

Player::SpriteState Player::getSpriteState()
{
	MotionType motion;
	if (interacting_)
	{
		motion = INTERACTING;
	}
	else if (on_ground())
	{
		motion = acceleration_x_ != 0 ? WALKING : STANDING;
	}
	else
	{
		motion = velocity_y_ < 0.0f ? JUMPING : FALLING;
	}
	return SpriteState(
		motion,
		horizontal_facing_,
		vertical_facing_);
}

void Player::update(units::MS elapsed_time_ms, const Map& map)
{
	sprites_[getSpriteState()]->update(elapsed_time_ms);

	health_.update(elapsed_time_ms);
	damage_text_.update(elapsed_time_ms);

	updateX(elapsed_time_ms, map);
	updateY(elapsed_time_ms, map);
}

void Player::draw(Graphics& graphics)
{
	if (spriteIsVisible())
	{
		polar_star_.draw(graphics, horizontal_facing_, vertical_facing_, x_, y_);
		sprites_[getSpriteState()]->draw(graphics, x_, y_);
	}
}

void Player::drawHUD(Graphics & graphics)
{
	if (spriteIsVisible())
	{
		health_.draw(graphics);
	}
	damage_text_.draw(graphics, center_x(), center_y());
}

void Player::startMovingLeft()
{
	acceleration_x_ = -1;
	horizontal_facing_ = LEFT;
	interacting_ = false;
}

void Player::startMovingRight()
{
	acceleration_x_ = 1;
	horizontal_facing_ = RIGHT;
	interacting_ = false;
}

void Player::stopMoving()
{
	acceleration_x_ = 0;
}

void Player::lookUp()
{
	vertical_facing_ = UP;
	interacting_ = false;
}

void Player::lookDown()
{
	if (vertical_facing_ == DOWN) return;
	vertical_facing_ = DOWN;
	interacting_ = on_ground();
}

void Player::lookHorizontal()
{
	vertical_facing_ = HORIZONTAL;
}

void Player::startJump()
{
	jump_active_ = true;
	interacting_ = false;

	if (on_ground())
	{
		velocity_y_ = -kJumpSpeed;
	}
}

void Player::stopJump()
{
	jump_active_ = false;
}

void Player::takeDamage(units::HP damage)
{
	if (invincible_timer_.active())
		return;

	health_.takeDamage(damage);
	damage_text_.setDamage(damage);

	velocity_y_ = std::min(velocity_y_, -kShortJumpSpeed);

	printf("Do damage to Quote!\n");

	invincible_timer_.reset();
}

Rectangle Player::damageRectangle() const
{
	return Rectangle(
		x_ + kCollisionX.left(),
		y_ + kCollisionY.top(),
		kCollisionX.width(),
		kCollisionY.height());
}

Rectangle Player::leftCollision(units::Game delta) const
{
	assert(delta <= 0);
	return Rectangle(
		x_ + kCollisionX.left() + delta,
		y_ + kCollisionX.top(),
		kCollisionX.width() / 2 - delta,
		kCollisionX.height());
}

Rectangle Player::rightCollision(units::Game delta) const
{
	assert(delta >= 0);
	return Rectangle(
		x_ + kCollisionX.left() + kCollisionX.width() / 2,
		y_ + kCollisionX.top(),
		kCollisionX.width() / 2 + delta,
		kCollisionX.height());
}

Rectangle Player::topCollision(units::Game delta) const
{
	assert(delta <= 0);
	return Rectangle(
		x_ + kCollisionY.left(),
		y_ + kCollisionY.top() + delta,
		kCollisionY.width(),
		kCollisionY.height() / 2 - delta);
}

Rectangle Player::bottomCollision(units::Game delta) const
{
	assert(delta >= 0);
	return Rectangle(
		x_ + kCollisionY.left(),
		y_ + kCollisionY.top() + kCollisionY.height() / 2,
		kCollisionY.width(),
		kCollisionY.height() / 2 + delta);
}

void Player::updateX(units::MS elapsed_time_ms, const Map& map)
{
	// Update velocity
	units::Acceleration acceleration_x = 0.0f;

	acceleration_x = on_ground() ? kWalkingAcceleration : kAirAcceleration;
	acceleration_x *= acceleration_x_;

	velocity_x_ += acceleration_x * elapsed_time_ms;
	if (acceleration_x_ < 0)
	{
		velocity_x_ = std::max(velocity_x_, -kMaxSpeedX);
	}
	else if (acceleration_x_ > 0)
	{
		velocity_x_ = std::min(velocity_x_, kMaxSpeedX);
	}
	else if (on_ground())
	{
		velocity_x_ = velocity_x_ > 0.0f ? 
			std::max(0.0f, velocity_x_ - kFriction * elapsed_time_ms) :
			std::min(0.0f, velocity_x_ + kFriction * elapsed_time_ms);
	}

	// Calculate delta
	const units::Game delta = velocity_x_ * elapsed_time_ms;
	if (delta > 0.0f)
	{
		// Check collision in the direction of delta (right)
		CollisionInfo info = getWallCollisionInfo(map, rightCollision(delta));

		// React to collision
		if (info.collided)
		{
			x_ = units::tileToGame(info.col) - kCollisionX.right();
			velocity_x_ = 0.0f;
		}
		else
		{
			x_ += delta;
		}

		// Check collision in the other direction
		info = getWallCollisionInfo(map, leftCollision(0));
		if (info.collided)
		{
			x_ = units::tileToGame(info.col) + kCollisionX.right();
		}
	}
	else
	{
		//Check collision in the direction of the delta (left)
		CollisionInfo info = getWallCollisionInfo(map, leftCollision(delta));

		//React to collision
		if (info.collided)
		{
			x_ = units::tileToGame(info.col) + kCollisionX.right();
			velocity_x_ = 0.0f;
		}
		else
		{
			x_ += delta;
		}

		//Check collision in other direction
		info = getWallCollisionInfo(map, rightCollision(0));
		if (info.collided)
		{
			x_ = units::tileToGame(info.col) - kCollisionX.right();
			on_ground_ = true;
		}
		
	}
}

void Player::updateY(units::MS elapsed_time_ms, const Map& map)
{
	// Update velocity
	const units::Acceleration gravity = jump_active_ && velocity_y_ < 0.0f ? 
		kJumpGravity : kGravity;

	velocity_y_ = std::min(velocity_y_ + gravity * elapsed_time_ms, 
			kMaxSpeedY);

	// Calculate delta
	const units::Game delta = velocity_y_ * elapsed_time_ms;

	if (delta > 0)
	{
		// Check collision in the direction of delta (down)
		CollisionInfo info = getWallCollisionInfo(map, bottomCollision(delta));

		// React to collision
		if (info.collided)
		{
			y_ = units::tileToGame(info.row) - kCollisionY.bottom();
			velocity_y_ = 0.0f;
			on_ground_ = true;
		}
		else
		{
			y_ += delta;
			on_ground_ = false;
		}

		// Check collision in the other direction
		info = getWallCollisionInfo(map, topCollision(0));
		if (info.collided)
		{
			y_ = units::tileToGame(info.row) + kCollisionY.height();
		}
	}
	else
	{
		//Check collision in the direction of the delta (up)
		CollisionInfo info = getWallCollisionInfo(map, topCollision(delta));

		//React to collision
		if (info.collided)
		{
			y_ = units::tileToGame(info.row) + kCollisionY.height();
			velocity_y_ = 0.0f;
			on_ground_ = false;
		}
		else
		{
			y_ += delta;
			on_ground_ = false;
		}

		//Check collision in other direction
		info = getWallCollisionInfo(map, bottomCollision(0));
		if (info.collided)
		{
			y_ = units::tileToGame(info.row) - kCollisionY.bottom();
			on_ground_ = true;
		}
		
	}
}

bool Player::spriteIsVisible() const
{
	 return !(invincible_timer_.active() 
		 && invincible_timer_.current_time() / kInvincibleFlashTime % 2 == 0);
}
