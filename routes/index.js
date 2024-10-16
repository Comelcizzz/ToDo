const userController = require('../controllers/user-controller');
const { body, param } = require('express-validator');
const authMiddleware = require('../middlewares/auth-middleware');
const projectController = require('../controllers/project-controller');
const taskController = require('../controllers/task-controller');
const commentController = require('../controllers/comment-controller');

const Router = require('express').Router;
const router = new Router();

// Route for root
router.get('/', (req, res) => {
    res.send('API is working!'); // Response to GET /
});

// User routes
router.post('/user/registration',
    body('email').isEmail().withMessage('Invalid email'),
    body('password').isLength({ min: 6, max: 24 }).withMessage('Password must be between 6 and 24 characters'),
    userController.registration);
    
router.post('/user/login', 
    body('email').isEmail().withMessage('Invalid email'),
    body('password').notEmpty().withMessage('Password is required'),
    userController.login);

router.post('/user/logout', authMiddleware, userController.logout);

router.post('/user/refresh', 
    body('newEmail').isEmail().withMessage('Invalid email'),
    userController.updateEmail);

// Project routes
router.post('/project/create', 
    authMiddleware, 
    body('name').notEmpty().withMessage('Project name is required'),
    projectController.createProject);

router.get('/project', authMiddleware, projectController.getProject);

router.put('/project/update', 
    authMiddleware,
    body('projectId').isUUID().withMessage('Invalid project ID'),
    body('name').optional().notEmpty().withMessage('Project name cannot be empty'),
    projectController.updateProject);

router.post('/project/delete', 
    authMiddleware, 
    body('projectId').isUUID().withMessage('Invalid project ID'),
    projectController.deleteProject);

// Task routes
router.post('/project/tasks/get', 
    authMiddleware, 
    body('projectId').isUUID().withMessage('Invalid project ID'),
    taskController.getTasks);

router.put('/project/tasks/update', 
    authMiddleware, 
    body('taskId').isUUID().withMessage('Invalid task ID'),
    body('status').notEmpty().withMessage('Task status is required'),
    taskController.updateTask);

router.post('/project/tasks/create', 
    authMiddleware, 
    body('projectId').isUUID().withMessage('Invalid project ID'),
    body('title').notEmpty().withMessage('Task title is required'),
    taskController.createTask);

router.post('/project/tasks/delete', 
    authMiddleware, 
    body('taskId').isUUID().withMessage('Invalid task ID'),
    taskController.deleteTask);

// Comment routes
router.post('/project/comments/create', 
    authMiddleware, 
    body('taskId').isUUID().withMessage('Invalid task ID'),
    body('content').notEmpty().withMessage('Comment content is required'),
    commentController.create);

router.post('/project/comments/delete', 
    authMiddleware, 
    body('commentId').isUUID().withMessage('Invalid comment ID'),
    commentController.delete);

module.exports = router;
