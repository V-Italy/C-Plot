#include "Application.h"
#include "picoc.h"

#define NUM_POINTS 1024
#define CURVE_WIDTH 32

void Application::init()
{
	// Create the window
	mWindow.create(sf::VideoMode(1000, 700), "C-Plot", 7, sf::ContextSettings(8, 8, 8));
	mWindow.setFramerateLimit(60);

	mWindow.setActive();

	// Enable Z-buffer read and write
	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glClearDepth(1.f);

	// Disable lighting
	glDisable(GL_LIGHTING);

	// Configure the viewport (the same size as the window)
	glViewport(mWindow.getSize().x * 0.25f, 0, mWindow.getSize().x*0.75f, mWindow.getSize().y);

	// Setup a perspective projection
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	GLfloat ratio = static_cast<float>(mWindow.getSize().x) / mWindow.getSize().y;
	glFrustum(-ratio, ratio, -1.f, 1.f, 1.f, 500.f);

	glEnableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
	glDisableClientState(GL_NORMAL_ARRAY);
	glEnableClientState(GL_COLOR_ARRAY);

	mGui.setWindow(mWindow);

	try
	{
		// Load the widgets
		loadWidgets();
	}
	catch (const tgui::Exception& e)
	{
		std::cerr << "Failed to load TGUI widgets: " << e.what() << std::endl;
		system("pause");
		return ;
	}

	// launch a thread with the parser
	mThread = std::thread(&Application::execute, this);
}

int Application::main()
{
	// Main loop
	sf::Clock timer;
	bool drag = false;
	sf::Vector2f dragPosition;
	sf::Vector2i dragMousePosition;

	while (mWindow.isOpen())
	{
		//***************************************************
		// Events and inputs
		//***************************************************
		sf::Event event;
		while (mWindow.pollEvent(event))
		{
			// When the window is closed, the application ends
			if (event.type == sf::Event::Closed)
				mWindow.close();

			// When the window is resized, the view is changed
			else if (event.type == sf::Event::Resized)
			{
				mWindow.setView(sf::View(sf::FloatRect(0, 0, (float)event.size.width, (float)event.size.height)));
				mGui.setView(mWindow.getView());
				glViewport(0, 0, event.size.width, event.size.height);
			}

			// Pass the event to all the widgets
			mGui.handleEvent(event);
		}

		// Zoom in
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::PageUp) && timer.getElapsedTime().asMilliseconds() > 10)
		{
			timer.restart();
			sf::Vector2f center(mGraphRect.left + 0.5f * mGraphRect.width, mGraphRect.top + 0.5f * mGraphRect.height);
			sf::Lock lock(mMutex);
			mGraphRect.width *= 1.02f;
			mGraphRect.height *= 1.02f;
			mGraphRect.left = center.x - 0.5f * mGraphRect.width;
			mGraphRect.top = center.y - 0.5f * mGraphRect.height;
		}
		// Zoom out
		if (sf::Keyboard::isKeyPressed(sf::Keyboard::PageDown) && timer.getElapsedTime().asMilliseconds() > 10)
		{
			timer.restart();
			sf::Vector2f center(mGraphRect.left + 0.5f * mGraphRect.width, mGraphRect.top + 0.5f * mGraphRect.height);
			sf::Lock lock(mMutex);
			mGraphRect.width *= 0.98f;
			mGraphRect.height *= 0.98f;
			mGraphRect.left = center.x - 0.5f * mGraphRect.width;
			mGraphRect.top = center.y - 0.5f * mGraphRect.height;
		}
		// mouse
		if (sf::Mouse::isButtonPressed(sf::Mouse::Left))
		{
			if (drag)
			{
				sf::Vector2i delta = sf::Mouse::getPosition() - dragMousePosition;
				const float sensibility = 0.001f;
				mGraphRect.left = dragPosition.x - delta.x * sensibility * mGraphRect.width;
				mGraphRect.top = dragPosition.y + delta.y * sensibility * mGraphRect.height;
			}
			else
			{
				drag = true;
				dragPosition = sf::Vector2f(mGraphRect.left, mGraphRect.top);
				dragMousePosition = sf::Mouse::getPosition();
			}
		}
		else
		{
			drag = false;
		}


		//***************************************************
		// Rendering
		//***************************************************
		mWindow.clear();

		// Draw all created widgets
		mWindow.pushGLStates();
		mGui.draw();

		// Curve
		mGraphScreen = sf::FloatRect(mGui.getSize().x * 0.25f + 30.f, 100.f, mGui.getSize().x * 0.65f, mGui.getSize().y - 200.f);

		if (mShowFunctionList)
		{
			showBuiltInFunctions();
		}
		else if (mCoordinate == THREE_D)
		{
			mWindow.popGLStates();
			show3DGraph();
			mWindow.pushGLStates();
		}
		else
		{
			showGraph();
		}

		// Display messages
		mMutex.lock();
		mErrorMessage.setPosition(30, mGui.getSize().y - 100);
		mWindow.draw(mErrorMessage);
		mMutex.unlock();

		// Progression bar
		sf::RectangleShape bar (sf::Vector2f(mProgression * 0.25f * mGui.getSize().x, 3.f));
		bar.setPosition(0, 15);
		bar.setFillColor(sf::Color(50, 50, 255));
		bar.setOutlineThickness(1.f);
		bar.setOutlineColor(sf::Color::Blue);
		mWindow.draw(bar);

		mWindow.popGLStates();
		mWindow.display();
	}

	mThread.detach();
	return EXIT_SUCCESS;
}

void Application::execute()
{
	std::vector<sf::Vector2f> result2D;
	std::vector<sf::Vector3f> result3D;
	
	while (1)
	{
		mMutex.lock();
		enumCoordinate coordinate = mCoordinate;
		mMutex.unlock();

		if (coordinate != THREE_D)
		{
			result2D.clear();
			evaluate2D(result2D, coordinate);
		}
		else // 3D curve
		{
			result3D.clear();
			evaluate3D(result3D);
		}

		mMutex.lock();
		if (coordinate == mCoordinate)
		{
			if (coordinate != THREE_D)
			{
				mPoints2D = result2D;
			}
			else // 3d curve
			{
				mPoints3D = result3D;
			}
		}
		mMutex.unlock();
		//std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

void Application::evaluate2D(std::vector<sf::Vector2f>& result, enumCoordinate coordinate)
{
	mMutex.lock();
	float width = mGraphRect.width;
	float start = mGraphRect.left;
	std::string buffer = mSourceCode;
	mMutex.unlock();
	int isCrash = 0;
	char errorBuffer[1024];

	for (int i = 0; i < NUM_POINTS; i++)
	{
		double x = (double)i / NUM_POINTS;
		mProgression = (float)x;
		if (coordinate == CARTESIAN)
		{
			x = x * width + start;
		}
		else
		{
			x *= 6.283185307179586;
		}

		float y = (float)parse(buffer.c_str(), &x, 1, &isCrash, errorBuffer);
		if (isCrash)
		{
			break;
		}

		result.push_back(sf::Vector2f((float)x, y));
	}
	if (isCrash)
		mErrorMessage.setString(errorBuffer);
	else
		mErrorMessage.setString(sf::String());
}

void Application::evaluate3D(std::vector<sf::Vector3f>& result)
{
	mMutex.lock();
	float width = mGraphRect.width;
	float start = mGraphRect.left;
	std::string buffer = mSourceCode;
	mMutex.unlock();
	int isCrash = 0;
	char errorBuffer[1024];

	double point[2];
	for (int i = 0; i < CURVE_WIDTH; i++)
	{
		double posX = (double)i / CURVE_WIDTH;
		mProgression = (float)posX;
		point[0] = posX * width + start;

		for (int j = 0; j < CURVE_WIDTH; j++)
		{
			double posY = (double)j / CURVE_WIDTH;
			point[1] = posY * width + start;

			float z = (float)parse(buffer.c_str(), point, 2, &isCrash, errorBuffer);
			if (isCrash)
			{
				break;
			}

			result.push_back(sf::Vector3f(1.f * (float)(posX-0.5f), 1.f * (float)(posY-0.5f), z));
		}
	}
	if (isCrash)
		mErrorMessage.setString(errorBuffer);
	else
		mErrorMessage.setString(sf::String());
}

void Application::showGraph()
{
	std::vector<sf::Vertex> lines;
	mMutex.lock();
	for (const sf::Vector2f& p : mPoints2D)
	{
		if (mCoordinate == CARTESIAN)
		{
			lines.push_back(convertGraphCoordToScreen(p));
		}
		else // polar coordinate
		{
			sf::Vector2f p(p.y * cos(p.x), p.y * sin(p.x));
			lines.push_back(convertGraphCoordToScreen(p));
		}
	}
	mMutex.unlock();
	mGui.getWindow()->draw(lines.data(), lines.size(), sf::LinesStrip);
	lines.clear();

	// Axis
	std::vector<sf::Vertex> axis;
	// horizontal
	float middleY = 1.f + mGraphRect.top / mGraphRect.height;
	lines.push_back(sf::Vector2f(mGraphScreen.left, mGraphScreen.top + middleY*mGraphScreen.height));
	lines.push_back(sf::Vector2f(mGraphScreen.left + mGraphScreen.width, mGraphScreen.top + middleY*mGraphScreen.height));
	//vertical
	float middleX = -mGraphRect.left / mGraphRect.width;
	lines.push_back(sf::Vector2f(mGraphScreen.left + middleX*mGraphScreen.width, mGraphScreen.top));
	lines.push_back(sf::Vector2f(mGraphScreen.left + middleX*mGraphScreen.width, mGraphScreen.top + mGraphScreen.height));

	std::vector<float> graduation = computeAxisGraduation(mGraphRect.left, mGraphRect.left + mGraphRect.width);
	const float graduationSize = 2.f;
	for (float x : graduation)
	{
		char str[32];
		sprintf_s<32>(str, "%g", x);
		sf::Text text(str, *mGui.getFont(), 12);
		x = (x - mGraphRect.left) / mGraphRect.width;
		text.setPosition(mGraphScreen.left + x * mGraphScreen.width, mGraphScreen.top + middleY*mGraphScreen.height - graduationSize);
		mGui.getWindow()->draw(text);

		lines.push_back(sf::Vector2f(mGraphScreen.left + x * mGraphScreen.width, mGraphScreen.top + middleY*mGraphScreen.height + graduationSize));
		lines.push_back(sf::Vector2f(mGraphScreen.left + x * mGraphScreen.width, mGraphScreen.top + middleY*mGraphScreen.height - graduationSize));
	}

	graduation = computeAxisGraduation(mGraphRect.top, mGraphRect.top + mGraphRect.height);
	for (float y : graduation)
	{
		char str[32];
		sprintf_s<32>(str, "%g", y);
		sf::Text text(str, *mGui.getFont(), 12);
		y = (y - mGraphRect.top) / mGraphRect.height;
		text.setPosition(mGraphScreen.left + middleX*mGraphScreen.width + graduationSize + 1.f, mGraphScreen.top + (1.f - y) * mGraphScreen.height - 5.f);
		mWindow.draw(text);

		lines.push_back(sf::Vector2f(mGraphScreen.left + middleX*mGraphScreen.width + graduationSize, mGraphScreen.top + (1.f - y) * mGraphScreen.height));
		lines.push_back(sf::Vector2f(mGraphScreen.left + middleX*mGraphScreen.width - graduationSize, mGraphScreen.top + (1.f - y) * mGraphScreen.height));
	}
	mWindow.draw(lines.data(), lines.size(), sf::Lines);

	sf::Vector2f mouse = convertScreenCoordToGraph(sf::Vector2f((float)sf::Mouse::getPosition(mWindow).x, (float)sf::Mouse::getPosition(mWindow).y));
	
	if (mouse.x >= mGraphRect.left && mouse.x <= mGraphRect.left + mGraphRect.width)
	{
		if (mCoordinate == POLAR)
		{
			mouse.x = atan2(mouse.y, mouse.x);
			if (mouse.x < 0)
				mouse.x += 6.283185307179586f;
		}

		float y = getAccurateYValue(mouse.x);
		char str[64];
		sprintf_s<64>(str, "(%g, %g)", mouse.x, y);
		sf::Text text(str, *mGui.getFont(), 12);
		sf::Vector2f textPos;
		if (mCoordinate == CARTESIAN)
		{
			textPos = convertGraphCoordToScreen(sf::Vector2f(0.f, y));
			textPos.x = (float)sf::Mouse::getPosition(mWindow).x;
		}
		else // polar coordinate
		{
			textPos = sf::Vector2f(y * cos(mouse.x), y * sin(mouse.x));
			textPos = convertGraphCoordToScreen(textPos);
		}
		text.setPosition(textPos);
		mWindow.draw(text);
		sf::RectangleShape rect(sf::Vector2f(3.f,3.f));
		rect.setPosition(textPos.x - 1.5f, textPos.y - 1.5f);
		rect.setFillColor(sf::Color(128,128,255));
		mWindow.draw(rect);
	}
}

void Application::show3DGraph()
{
	mMutex.lock();
	if (mPoints3D.empty())
	{
		mMutex.unlock();
		return;
	}

	// Clear the depth buffer
	glClear(GL_DEPTH_BUFFER_BIT);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glTranslatef(0.f, 0.f, -4.f);
	static sf::Clock clock;
	glRotatef(30.f, -1.f, 1.f, 0.f);
	glRotatef(clock.getElapsedTime().asSeconds() * 30.f, 0.f, 0.f, 1.f);
	float scale = 3.f;
	glScalef(scale, scale, scale);

	std::vector<sf::Vector3f> positions;
	std::vector<sf::Color> colors;

	float minZ = 0, maxZ = 0;
	for (const sf::Vector3f& p : mPoints3D)
	{
		if (minZ > p.z)
			minZ = p.z;
		if (maxZ < p.z)
			maxZ = p.z;
	}
	float deltaZ = 0.f;
	if (maxZ - minZ > 1e-7f)
		deltaZ = 1.f / (maxZ - minZ);

	for (int x = 0; x < CURVE_WIDTH-1; x++)
	{
		for (int y = 0; y < CURVE_WIDTH-1; y++)
		{
			sf::Vector3f p0 = mPoints3D[x * CURVE_WIDTH + y];
			sf::Vector3f p1 = mPoints3D[(x+1) * CURVE_WIDTH + y];
			sf::Vector3f p2 = mPoints3D[(x+1) * CURVE_WIDTH + y + 1];
			sf::Vector3f p3 = mPoints3D[x * CURVE_WIDTH + y + 1];
			sf::Color c0 = rainbowColor(p0.z = (p0.z - minZ) * deltaZ);
			sf::Color c1 = rainbowColor(p1.z = (p1.z - minZ) * deltaZ);
			sf::Color c2 = rainbowColor(p2.z = (p2.z - minZ) * deltaZ);
			sf::Color c3 = rainbowColor(p3.z = (p3.z - minZ) * deltaZ);
			p0.z -=  0.5f; p1.z -= 0.5f; p2.z -= 0.5f; p3.z -= 0.5f;

			positions.push_back(p0);
			positions.push_back(p1);
			positions.push_back(p2);
			positions.push_back(p2);
			positions.push_back(p3);
			positions.push_back(p0);
			colors.push_back(c0);
			colors.push_back(c1);
			colors.push_back(c2);
			colors.push_back(c2);
			colors.push_back(c3);
			colors.push_back(c0);
		}
	}
	mMutex.unlock();

	glVertexPointer(3, GL_FLOAT, 3 * sizeof(float), positions.data());
	glColorPointer(4, GL_UNSIGNED_BYTE, 4 * sizeof(unsigned char), colors.data());

	glDrawArrays(GL_TRIANGLES, 0, positions.size());
}

void Application::callbackTextEdit(tgui::TextBox::Ptr source)
{
	mMutex.lock();
	mSourceCode = source->getText().toAnsiString();
	mMutex.unlock();
}

void Application::fillDefaultSourceCode()
{
	if (mCoordinate != THREE_D)
	{
		mSourceCodeEditBox->setText("double main(double x){\n\nreturn x;\n}");
	}
	else
	{
		mSourceCodeEditBox->setText("double main(double x, double y){\n\nreturn fabs(sin(x*0.5));\n}");
		//mSourceCodeEditBox->setText("double main(double x, double y){\n\nreturn sin(x)*sin(y);\n}");
	}
	callbackTextEdit(mSourceCodeEditBox);
}

void Application::showBuiltInFunctions()
{
	std::string list;
	GetBuiltInFunction(list);
	const char* str = list.c_str();

	sf::Text text("", *mGui.getFont(), 12);
	text.setPosition(mGui.getSize().x * 0.25f + 30.f, 30.f);
	text.setColor(sf::Color::White);

	const char* strEnd = str + list.length();

	for (const char* splitEnd = str; splitEnd != strEnd; ++splitEnd)
	{
		if (*splitEnd == '\n')
		{
			const ptrdiff_t splitLen = splitEnd - str;
			text.setString(std::string(str, splitLen));
			str = splitEnd + 1;

			sf::Vector2f pos = text.getPosition();
			pos.y += 15.f;
			if (pos.y > mGui.getSize().y - 50)
			{
				pos = sf::Vector2f(pos.x + 250.f, 30.f);
			}
			text.setPosition(pos);
			mGui.getWindow()->draw(text);
		}
	}
}

void Application::loadWidgets()
{
	// Load the black theme
	auto theme = std::make_shared<tgui::Theme>();// "TGUI/widgets/Black.txt");

	auto windowWidth = tgui::bindWidth(mGui);
	auto windowHeight = tgui::bindHeight(mGui);


	// Create the source code edit box
	mSourceCodeEditBox = theme->load("TextBox");
	mSourceCodeEditBox->setSize(windowWidth * 0.25f, windowHeight - 200);
	mSourceCodeEditBox->setPosition(10, 30);
	mGui.add(mSourceCodeEditBox, "Code");
	mSourceCodeEditBox->connect("TextChanged", &Application::callbackTextEdit, this, mSourceCodeEditBox);
	
	// Apply default source code
	fillDefaultSourceCode();

	// Create the login button
	tgui::Button::Ptr button = theme->load("Button");
	button->setSize(windowWidth * 0.25f, 25);
	button->setPosition(10, windowHeight -150);
	button->setText("Show built-in functions");
	mGui.add(button);
	button->connect("pressed", [this] {
		mShowFunctionList = !mShowFunctionList;
	});

	tgui::ComboBox::Ptr coordinateBox = theme->load("ComboBox");
	coordinateBox->setSize(170, 20);
	coordinateBox->setPosition(windowWidth * 0.25f + 60.f, 10.f);
	coordinateBox->addItem("Cartesian coordinates");
	coordinateBox->addItem("Polar coordinates");
	coordinateBox->addItem("3D curve");
	coordinateBox->setSelectedItemByIndex(mCoordinate);
	mGui.add(coordinateBox);
	coordinateBox->connect("ItemSelected", [this](tgui::ComboBox::Ptr box) {
		mPoints2D.clear();
		mPoints3D.clear();
		if (mCoordinate != (enumCoordinate)box->getSelectedItemIndex())
		{
			mCoordinate = (enumCoordinate)box->getSelectedItemIndex();
			fillDefaultSourceCode();
		}
		mCoordinate = (enumCoordinate)box->getSelectedItemIndex();
	}, coordinateBox);

	mErrorMessage.setFont(*mGui.getFont());
	mErrorMessage.setCharacterSize(13);
	mErrorMessage.setColor(sf::Color::Red);
}

sf::Vector2f Application::convertGraphCoordToScreen(const sf::Vector2f& point) const
{
	sf::Vector2f p((point.x - mGraphRect.left) / mGraphRect.width, (point.y - mGraphRect.top) / mGraphRect.height);
	return sf::Vector2f(mGraphScreen.left + p.x * mGraphScreen.width, mGraphScreen.top + (1.f - p.y) * mGraphScreen.height);
}

sf::Vector2f Application::convertScreenCoordToGraph(const sf::Vector2f& point) const
{
	sf::Vector2f p((point.x - mGraphScreen.left) / mGraphScreen.width, (point.y - mGraphScreen.top) / mGraphScreen.height);
	return sf::Vector2f(p.x * mGraphRect.width + mGraphRect.left, (1.f-p.y) * mGraphRect.height + mGraphRect.top);
}

std::vector<float> Application::computeAxisGraduation(float min, float max) const
{
	float delta = max - min;
	const static double mul[] = { 1.0, 2.0, 5.0 };
	std::vector<float> axis;

	bool ok = false;
	double step = FLT_MAX;
	for (int e = -7; e < 9; e++)
	{
		double a = pow(10.0, e);

		for (int i = 0; i < 3; i++)
		{
			double b = a * mul[i];

			if (delta / b <= 10)
			{
				step = b;
				ok = true;
				break;
			}
		}
		if (ok)
		{
			break;
		}
	}

	double i = floor(min / step) * step;
	for (; i < max + 0.1*step; i += step)
	{
		if (abs(i) > 1e-9)
		{
			axis.push_back((float)i);
		}
	}

	return axis;
}

float Application::getAccurateYValue(float x) const
{
	if (mPoints2D.size() < 2)
		return 0.f;

	sf::Lock lock(mMutex);

	sf::Vector2f p0 = mPoints2D[0];
	sf::Vector2f p1 = mPoints2D[1];
	for (unsigned i = 1; i < mPoints2D.size(); i++)
	{
		if (mPoints2D[i].x > x)
		{
			p0 = mPoints2D[i-1];
			p1 = mPoints2D[i];
			break;
		}
	}

	float a = (x - p0.x) / (p1.x - p0.x);
	return a * (p1.y - p0.y) + p0.y;
}

//i entre 0 et 1
sf::Color Application::rainbowColor(float i)
{
	if (i < 0.16667f)
	{
		return sf::Color(255, 0, i * 6.f*255.f);
	}
	else if (i < 0.33333f)
	{
		return sf::Color(255.f - (i - 0.166667f) * 6.f * 255.f, 0, 255);
	}
	else if (i < 0.5f)
	{
		return sf::Color(0, (i - 0.33333f) * 6.f * 255.f, 255);
	}
	else if (i < 0.66667f)
	{
		return sf::Color(0, 255, 255 - (i - 0.5f) * 6.f * 255.f);
	}
	else if (i < 0.83333f)
	{
		return sf::Color((i - 0.66667f) * 6.f * 255.f, 255, 0);
	}
	else
	{
		return sf::Color(255, 255 - (i - 0.83333f) * 6.f * 255.f, 0);
	}
}